// Development simulator parser for the API's flat command objects.
// JSON.parse validates syntax but silently discards duplicate keys, so inspect
// every complete string token before accepting the parsed object.
export function parseCommand(body) {
  const value = JSON.parse(body);
  if (!value || typeof value !== 'object' || Array.isArray(value) ||
      Object.values(value).some((field) => field !== null && typeof field === 'object')) {
    throw new Error('Expected a flat command object');
  }
  const keys = new Set();
  for (const match of body.matchAll(/"(?:[^"\\]|\\.)*"/g)) {
    // Consume value strings as whole tokens too, so text resembling a key
    // inside an escaped string cannot be mistaken for an object member.
    if (!/^\s*:/.test(body.slice(match.index + match[0].length))) continue;
    const key = JSON.parse(match[0]);
    if (keys.has(key)) throw new Error('Duplicate command field');
    keys.add(key);
  }
  return value;
}
