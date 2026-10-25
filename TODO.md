# TODO

## Per-song organ stops (rejstříky)

At the start of a song the organ's stops (rejstříky, e.g. bass flutes) must be set.

- Each stop is switched by one MIDI command; the set of stops differs per song.
- The user needs a way to define this per song: a per-song stops file
  (e.g. next to the song file), listing which stops are on.
- The stop commands are **toggles** and their state **cannot be read back** from the organ.
  So we must either:
  - track the current state of every stop in memory (and send only the toggles
    needed to get from the current state to the song's state), or
  - when the state is unknown (after startup, lost link, manual change), power-cycle
    the organ: all stops start **off** after power-on, so the state is known again.
- Open: where the tracked state lives (PC software vs. base firmware) and whether
  it must survive a restart.
- **Waiting on the user:** the MIDI command values for each stop.

## Organ power behaviour between songs

- Decide how organ power behaves between songs and during a session: stay on, turn off
  after a pause, power-cycle to reset the stops (see "Per-song organ stops" above),
  what happens at the start and end of a session.
