// Run with: node --test scripts/generate-cme-list.test.mjs  (npm run test:scripts in web/)
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { test } from 'node:test';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';
import { CURATED, donkiEvents, isoMinutes, mergeEvents, parseDonkiTime } from './generate-cme-list.mjs';

const root = join(dirname(fileURLToPath(import.meta.url)), '..');

const snapshot = {
  fetched: '2026-01-01',
  analyses: [
    {
      associatedCMEID: '2017-09-10T16:00:00-CME-001',
      time21_5: '2017-09-10T17:14Z',
      latitude: -9,
      longitude: 92,
      halfAngle: 55,
      speed: 2650,
    },
    {
      associatedCMEID: '2021-10-28T15:48:00-CME-001',
      time21_5: '2021-10-28T19:21Z',
      latitude: -26,
      longitude: 1,
      halfAngle: 42,
      speed: 1100,
    },
    // Too slow to keep.
    { associatedCMEID: '2021-11-01T00:00:00-CME-001', time21_5: '2021-11-01T04:00Z', halfAngle: 40, speed: 500 },
    // The same CME as a curated row (2017-09-06): the curated row wins.
    { associatedCMEID: '2017-09-06T12:24:00-CME-001', time21_5: '2017-09-06T13:30Z', halfAngle: 60, speed: 1571 },
  ],
  ips: [
    {
      activityID: '2021-10-30T22:08:00-IPS-001',
      location: 'Earth',
      eventTime: '2021-10-30T22:08Z',
      linkedEvents: [{ activityID: '2021-10-28T15:48:00-CME-001' }],
    },
    {
      activityID: '2017-09-12T19:00:00-IPS-001',
      location: 'STEREO A',
      eventTime: '2017-09-12T19:00Z',
      linkedEvents: [{ activityID: '2017-09-10T16:00:00-CME-001' }],
    },
  ],
  gst: [
    {
      gstID: '2021-11-04T06:00:00-GST-001',
      startTime: '2021-11-04T06:00Z',
      allKpIndex: [{ kpIndex: 7.67 }],
      linkedEvents: [],
    },
    {
      gstID: '2021-10-31T03:00:00-GST-001',
      startTime: '2021-10-31T03:00Z',
      allKpIndex: [{ kpIndex: 5 }, { kpIndex: 5.33 }],
      linkedEvents: [{ activityID: '2021-10-30T22:08:00-IPS-001' }],
    },
  ],
};

test('parses DONKI times and activity IDs to UTC milliseconds', () => {
  assert.equal(parseDonkiTime('2003-10-29T06:11Z'), Date.UTC(2003, 9, 29, 6, 11));
  assert.equal(parseDonkiTime('2021-10-28T15:48:00-CME-001'), Date.UTC(2021, 9, 28, 15, 48));
  assert.ok(Number.isNaN(parseDonkiTime('soon')));
  assert.equal(isoMinutes(Date.UTC(2024, 4, 10, 16, 36, 30)), '2024-05-10T16:36Z');
});

test('keeps fast wide CMEs with their Earth shock and storm Kp', () => {
  const events = donkiEvents(snapshot);
  const byId = new Map(events.map((e) => [e.id, e]));
  assert.ok(!byId.has('donki-2021-11-01T00:00:00-CME-001'), 'slow CME dropped');

  const earthward = byId.get('donki-2021-10-28T15:48:00-CME-001');
  assert.equal(earthward.launch, '2021-10-28T19:21Z');
  assert.equal(earthward.launchRadiusRsun, 21.5);
  assert.equal(earthward.earthArrival, '2021-10-30T22:08Z');
  // The linked storm (Kp 5.33), not the later unrelated one (7.67 starts > 2 days after).
  assert.equal(earthward.kpMax, 5.33);

  // A shock at STEREO A is not an Earth arrival.
  const farSide = byId.get('donki-2017-09-10T16:00:00-CME-001');
  assert.equal(farSide.earthArrival, undefined);
  assert.equal(farSide.kpMax, undefined);
});

test('curated rows win over DONKI rows for the same event, and the list is sorted', () => {
  const merged = mergeEvents(CURATED, donkiEvents(snapshot));
  const sept2017 = merged.filter((e) => e.launch.startsWith('2017-09-06'));
  assert.equal(sept2017.length, 1);
  assert.equal(sept2017[0].source, 'curated');
  for (let i = 1; i < merged.length; ++i) {
    assert.ok(parseDonkiTime(merged[i - 1].launch) <= parseDonkiTime(merged[i].launch));
  }
});

test('every curated row has what SpaceWeather::ParseCmeCatalog needs', () => {
  const ids = new Set();
  for (const row of CURATED) {
    assert.ok(row.id && !ids.has(row.id), `unique id ${row.id}`);
    ids.add(row.id);
    assert.ok(!Number.isNaN(parseDonkiTime(row.launch)), `${row.id} launch`);
    assert.ok(row.speedKmS > 0, `${row.id} speed`);
    if (row.earthArrival) {
      assert.ok(parseDonkiTime(row.earthArrival) > parseDonkiTime(row.launch), `${row.id} arrives after launch`);
    }
    if (row.kpMax !== undefined) assert.ok(row.kpMax >= 0 && row.kpMax <= 9);
  }
});

test('the committed cmes.json carries every curated storm', () => {
  const committed = JSON.parse(readFileSync(join(root, 'resource/spaceweather/cmes.json'), 'utf8'));
  const ids = new Set(committed.events.map((e) => e.id));
  for (const row of CURATED) assert.ok(ids.has(row.id), row.id);
});
