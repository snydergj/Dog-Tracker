const epsilon = 1e-12;

function orientation(a, b, p) {
  const cross = (b[1] - a[1]) * (p[0] - a[0]) - (b[0] - a[0]) * (p[1] - a[1]);
  return Math.abs(cross) <= epsilon ? 0 : Math.sign(cross);
}

function onSegment(a, b, point) {
  return orientation(a, b, point) === 0 && point.every((value, axis) =>
    value >= Math.min(a[axis], b[axis]) && value <= Math.max(a[axis], b[axis]));
}

function intersects(a, b, c, d) {
  return onSegment(a, b, c) || onSegment(a, b, d) || onSegment(c, d, a) || onSegment(c, d, b) ||
    (orientation(a, b, c) * orientation(a, b, d) < 0 && orientation(c, d, a) * orientation(c, d, b) < 0);
}

export function validFence(points) {
  if (!Array.isArray(points) || points.length < 3 || points.length > 40 ||
      !points.every(point => Array.isArray(point) && point.length === 2 &&
        point.every(Number.isFinite) && Math.abs(point[0]) <= 90 && Math.abs(point[1]) <= 180) ||
      new Set(points.map(point => point.join(','))).size !== points.length) return false;
  const longitudes = points.map(point => point[1]);
  if (Math.max(...longitudes) - Math.min(...longitudes) >= 180) return false;

  const origin = points[0];
  let area = 0;
  for (let i = 0; i < points.length; i++) {
    const a = points[i];
    const b = points[(i + 1) % points.length];
    area += (a[0] - origin[0]) * (b[1] - origin[1]) - (a[1] - origin[1]) * (b[0] - origin[0]);
    for (let j = i + 2; j < points.length; j++) {
      if (i === 0 && j === points.length - 1) continue;
      if (intersects(a, b, points[j], points[(j + 1) % points.length])) return false;
    }
  }
  return Math.abs(area) > epsilon;
}

export function insideFence(point, points) {
  const [latitude, longitude] = point;
  let inside = false;
  for (let i = 0, j = points.length - 1; i < points.length; j = i++) {
    const a = points[j];
    const b = points[i];
    if (onSegment(a, b, point)) return true;
    if ((a[0] > latitude) !== (b[0] > latitude) &&
        longitude < a[1] + (b[1] - a[1]) * (latitude - a[0]) / (b[0] - a[0])) inside = !inside;
  }
  return inside;
}

export function trackFenceCrossings() {
  let outsideAt = null;
  let lastReading = null;
  return {
    reset() { outsideAt = lastReading = null; },
    sample(inside, now) {
      if (inside || lastReading === null || now - lastReading > 5000) outsideAt = null;
      lastReading = now;
      if (inside) return 'inside';
      outsideAt ??= now;
      return now - outsideAt >= 3000 ? 'outside' : 'checking';
    },
  };
}
