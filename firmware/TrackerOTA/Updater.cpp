#include <Arduino.h>
#include <Update.h>
#include <cJSON.h>
#include <esp_crt_bundle.h>
#include <esp_http_client.h>
#include <esp_ota_ops.h>
#include <mbedtls/sha256.h>
#include <memory>

#include "Updater.h"
#include "Version.h"

namespace {
constexpr char kReleaseApi[] = "https://api.github.com/repos/snydergj/Dog-Tracker/releases/";
constexpr char kAssetName[] = "heltec-wireless-tracker-v2-ota.bin";

uint64_t versionNumber(const char *version) {
  if (!version) return 0;
  uint64_t result = 0;
  for (int part = 0; part < 3; ++part) {
    if (*version < '0' || *version > '9') return 0;
    if (*version == '0' && version[1] >= '0' && version[1] <= '9') return 0;
    unsigned number = 0;
    do {
      number = number * 10 + (*version++ - '0');
      if (number > 65535) return 0;
    } while (*version >= '0' && *version <= '9');
    if (*version++ != (part < 2 ? '.' : '\0')) return 0;
    result = (result << 16) | number;
  }
  return result;
}

struct Response {
  esp_http_client_handle_t client = nullptr;
  String location;
  int status = 0;
  int64_t length = -1;
  ~Response() { if (client) esp_http_client_cleanup(client); }
};

esp_err_t onHeader(esp_http_client_event_t *event) {
  if (event->event_id == HTTP_EVENT_ON_HEADER && strcasecmp(event->header_key, "Location") == 0) {
    static_cast<Response *>(event->user_data)->location = event->header_value;
  }
  return ESP_OK;
}

bool get(Response &response, const char *url, const char *token, const char *accept) {
  esp_http_client_config_t config{};
  config.url = url;
  config.crt_bundle_attach = esp_crt_bundle_attach;
  config.disable_auto_redirect = true;
  // GitHub's signed asset URL must fit in the HTTP request's first line.
  config.buffer_size_tx = 4096;
  config.timeout_ms = 7000;
  config.user_agent = "DogTracker-ESP32S3/" TRACKER_VERSION;
  config.event_handler = onHeader;
  config.user_data = &response;
  response.client = esp_http_client_init(&config);
  if (!response.client) return false;
  esp_http_client_set_header(response.client, "Accept", accept);
  if (token) {
    const String authorization = String("Bearer ") + token;
    if (esp_http_client_set_header(response.client, "Authorization", authorization.c_str()) != ESP_OK) return false;
  }
  const esp_err_t opened = esp_http_client_open(response.client, 0);
  if (opened != ESP_OK) {
    Serial.printf("\nOTA could not open HTTPS request (%s).\n", esp_err_to_name(opened));
    return false;
  }
  response.length = esp_http_client_fetch_headers(response.client);
  response.status = esp_http_client_get_status_code(response.client);
  return response.length >= 0 && response.status > 0;
}

struct Candidate {
  uint64_t id = 0;
  size_t size = 0;
  char digest[65]{};
};

Candidate latestRelease(const char *token) {
  const String url = String(kReleaseApi) + "latest";
  Response response;
  if (!get(response, url.c_str(), token, "application/vnd.github+json") || response.status != 200) {
    Serial.printf("OTA release check did not succeed (HTTP %d).\n", response.status);
    return {};
  }
  constexpr int kMaximumMetadata = 24576;
  std::unique_ptr<char[]> body(new (std::nothrow) char[kMaximumMetadata + 1]);
  if (!body || response.length > kMaximumMetadata) return {};
  const int received = esp_http_client_read_response(response.client, body.get(), kMaximumMetadata);
  if (received <= 0 || !esp_http_client_is_complete_data_received(response.client)) return {};
  body[received] = '\0';
  std::unique_ptr<cJSON, decltype(&cJSON_Delete)> release(cJSON_Parse(body.get()), cJSON_Delete);
  if (!release) return {};
  const char *tag = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(release.get(), "tag_name"));
  if (!tag || *tag != 'v' || versionNumber(tag + 1) <= versionNumber(TRACKER_VERSION)) {
    Serial.println("OTA found no newer release.");
    return {};
  }

  const esp_partition_t *target = esp_ota_get_next_update_partition(nullptr);
  if (!target) return {};
  const cJSON *assets = cJSON_GetObjectItemCaseSensitive(release.get(), "assets");
  const cJSON *asset;
  cJSON_ArrayForEach(asset, assets) {
    const char *name = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(asset, "name"));
    if (!name || strcmp(name, kAssetName) != 0) continue;
    const char *digest = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(asset, "digest"));
    const double size = cJSON_GetNumberValue(cJSON_GetObjectItemCaseSensitive(asset, "size"));
    const double id = cJSON_GetNumberValue(cJSON_GetObjectItemCaseSensitive(asset, "id"));
    if (!digest || strncmp(digest, "sha256:", 7) != 0 || strlen(digest + 7) != 64 ||
        strspn(digest + 7, "0123456789abcdef") != 64 || !(size > 0 && size <= target->size) ||
        !(id > 0 && id <= 9007199254740991.0)) return {};
    Candidate result;
    result.id = static_cast<uint64_t>(id);
    result.size = static_cast<size_t>(size);
    if (id != result.id || size != result.size) return {};
    memcpy(result.digest, digest + 7, 64);
    return result;
  }
  return {};
}

bool install(Response &response, const Candidate &candidate) {
  if (response.status != 200 || (response.length > 0 && response.length != candidate.size) ||
      !Update.begin(candidate.size, U_FLASH)) return false;

  mbedtls_sha256_context hash;
  mbedtls_sha256_init(&hash);
  bool complete = mbedtls_sha256_starts(&hash, 0) == 0;
  uint8_t buffer[2048];
  size_t received = 0;
  const unsigned long started = millis();
  while (complete && received < candidate.size && millis() - started < 120000) {
    const size_t limit = min(sizeof(buffer), candidate.size - received);
    const int count = esp_http_client_read(response.client, reinterpret_cast<char *>(buffer), limit);
    if (count <= 0) break;
    complete = mbedtls_sha256_update(&hash, buffer, count) == 0 && Update.write(buffer, count) == count;
    received += count;
    vTaskDelay(1);
  }
  unsigned char bytes[32];
  complete = complete && received == candidate.size && esp_http_client_is_complete_data_received(response.client) &&
             mbedtls_sha256_finish(&hash, bytes) == 0;
  mbedtls_sha256_free(&hash);
  char hex[65]{};
  if (complete) {
    for (size_t i = 0; i < sizeof(bytes); ++i) snprintf(hex + i * 2, 3, "%02x", bytes[i]);
    if (strcmp(hex, candidate.digest) == 0 && Update.end()) return true;
    Serial.println("\nOTA image checksum or activation failed.");
  } else {
    Serial.printf("\nOTA image incomplete (%u of %u bytes).\n", static_cast<unsigned>(received),
                  static_cast<unsigned>(candidate.size));
  }
  Update.abort();
  return false;
}
}

void checkForTrackerUpdate(const char *token) {
  const Candidate candidate = latestRelease(token);
  if (!candidate.id) return;
  Serial.println("\nOTA downloading a newer release.");
  char url[150];
  snprintf(url, sizeof(url), "%sassets/%llu", kReleaseApi, static_cast<unsigned long long>(candidate.id));
  String redirect;
  bool installed = false;
  {
    Response response;
    if (get(response, url, token, "application/octet-stream")) {
      if (response.status == 200) installed = install(response, candidate);
      else if (response.status == 302 || response.status == 307) redirect = response.location;
    }
  }
  if (redirect.startsWith("https://release-assets.githubusercontent.com/") ||
      redirect.startsWith("https://objects.githubusercontent.com/")) {
    Response asset;
    // The GitHub token is only sent to api.github.com.
    if (get(asset, redirect.c_str(), nullptr, "application/octet-stream")) installed = install(asset, candidate);
  }
  if (installed) {
    Serial.println("\nOTA verified. Restarting.");
    delay(300);
    ESP.restart();
  }
  Serial.println("\nOTA download or verification failed. The current firmware is still active.");
}
