function position(latitude, longitude, satellites = null) {
  if (!Number.isFinite(latitude) || Math.abs(latitude) > 90 ||
      !Number.isFinite(longitude) || Math.abs(longitude) > 180) return null;
  return { hasFix: true, latitude, longitude, satellites };
}

function degrees(value, hemisphere, axis) {
  const digits = axis === 'latitude' ? 2 : 3;
  const directions = axis === 'latitude' ? 'NS' : 'EW';
  if (!new RegExp(`^\\d{${digits + 2}}(?:\\.\\d+)?$`).test(value) ||
      !hemisphere || !directions.includes(hemisphere) || hemisphere.length !== 1) return NaN;
  const minutes = Number(value.slice(digits));
  if (minutes >= 60) return NaN;
  const decimal = Number(value.slice(0, digits)) + minutes / 60;
  return 'SW'.includes(hemisphere) ? -decimal : decimal;
}

export function parseGpsLine(line) {
  const factory = line.match(/(?:^|[^a-z])LAT\s*:\s*([+-]?\d+(?:\.\d+)?),\s*LON\s*:\s*([+-]?\d+(?:\.\d+)?)(?:\s|$)/i);
  if (factory) {
    const [latitude, longitude] = factory.slice(1).map(Number);
    if (latitude === 0 && longitude === 0) return { hasFix: false, satellites: null };
    return position(latitude, longitude);
  }

  const sentence = line.match(/^\$([A-Z]{2}(GGA|RMC),[^*]+)\*([0-9a-f]{2})$/i);
  if (!sentence) return null;
  const [, payload, type, checksum] = sentence;
  let calculated = 0;
  for (const character of payload) calculated ^= character.charCodeAt(0);
  if (calculated !== Number.parseInt(checksum, 16)) return null;

  const fields = payload.split(',');
  if (type.toUpperCase() === 'GGA') {
    const satellites = /^\d+$/.test(fields[7]) ? Number(fields[7]) : null;
    if (!/^[1-9]\d*$/.test(fields[6])) return { hasFix: false, satellites };
    return position(degrees(fields[2], fields[3], 'latitude'), degrees(fields[4], fields[5], 'longitude'), satellites);
  }
  if (fields[2] !== 'A') return { hasFix: false, satellites: null };
  return position(degrees(fields[3], fields[4], 'latitude'), degrees(fields[5], fields[6], 'longitude'));
}
