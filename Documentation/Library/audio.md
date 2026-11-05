# audio

`audio` plays, records, edits and changes sound. It has `AudioBuffer`, a sound held in memory that loads from WAV, FLAC, MP3, Ogg Vorbis, Opus and AAC files and from the sound of MP4, M4A, MOV, MKV and WebM videos, and saves to WAV, FLAC and MP3; over thirty effects (filters, EQ, compression, reverb, delay, pitch shifting, auto-tune, voice changing, noise reduction and more) that work on buffers or live; `Mixer` for playing many sounds at once, as a soundboard does; `Stream` for running your own Walnut code on the sound card's audio thread; `LiveChain`, which sends a microphone through effects to the speakers, as a voice changer does; and `Timeline`, with tracks, clips and automation, for putting songs together and exporting them.

It's Fizmo's audio library, which talks to the sound card itself (WASAPI on Windows, PulseAudio or ALSA on Linux, Core Audio on macOS, AAudio on Android) and has its own codecs, so programs need no other libraries and sound the same on every system.

```walnut
import module.audio
```

Everything is in the namespace `std::audio`, and errors are [`std::errors::audio_error`](#errors).

## Contents

1. [A first example](#a-first-example)
2. [AudioBuffer](#audiobuffer)
3. [Effects](#effects)
4. [Effect chains and presets](#effect-chains-and-presets)
5. [Playing sounds: Mixer](#playing-sounds-mixer)
6. [Devices](#devices)
7. [Streams: your own code on the audio thread](#streams-your-own-code-on-the-audio-thread)
8. [LiveChain: microphone to speakers through effects](#livechain-microphone-to-speakers-through-effects)
9. [Timeline](#timeline)
10. [Enums](#enums)
11. [Errors](#errors)
12. [Systems and backends](#systems-and-backends)

## A first example

```walnut
import module.audio
import module.io

using std::audio::AudioBuffer;
using std::audio::Effect;

function main() -> int {
    AudioBuffer: note = AudioBuffer::tone(440.0, 1.0, channels = 2);
    note.fade_out(0.3);
    Effect: room = Effect::reverb();
    room.set("decay", 1.5);
    AudioBuffer: wet = note.applied(room);
    std::io::println(note.duration(), " s became ", wet.duration(), " s");
    std::io::println("pitch: ", static_cast<long int>(note.pitch() + 0.5), " Hz");
    wet.save("note.wav");
    wet.save("note.mp3");
    std::audio::play(wet);
    return 0;
} end function main
```

This prints `1 s became 2.82 s` (the reverb's tail is kept) and `pitch: 440 Hz`, writes the sound to a WAV and an MP3 file, and plays it through the default speakers.

## AudioBuffer

An `AudioBuffer` is a sound in memory: a number of channels (1 for mono, 2 for stereo), a sample rate (samples per second, such as 48000) and its samples, as 32-bit floats from -1.0 to 1.0. One sample per channel at one moment is a frame, so a one-second stereo buffer at 48000 Hz has 48000 frames and 96000 samples. It's a value: copying a buffer copies its samples.

### Making and loading

| Written | Gives |
|---|---|
| `AudioBuffer()` | An empty buffer with no channels |
| `AudioBuffer::make(channels, sample_rate, frames = 0)` | Silence of that format and length |
| `AudioBuffer::silence(seconds, sample_rate = 48000, channels = 2)` | That many seconds of silence |
| `AudioBuffer::tone(frequency, seconds, sample_rate = 48000, channels = 1, amplitude = 0.5, shape = Waveform::sine)` | A test tone; `shape` is a [`Waveform`](#enums): `sine`, `square`, `saw`, `triangle` or `noise` |
| `AudioBuffer::from_samples(samples, channels, sample_rate)` | A buffer made from a `Vector<std::hardware_float>` of interleaved samples (left, right, left, right, ...) |
| `AudioBuffer::load(path)`, `std::audio::load(path)` | The sound in a WAV, FLAC, MP3, Ogg Vorbis, Ogg Opus or AAC (ADTS) file, or the main audio track of an MP4, M4A, MOV, Matroska (MKV, MKA) or WebM file (AAC, Opus, Vorbis, MP3, FLAC or PCM); the format is worked out from the file's contents |

`make`, `silence`, `tone` and `from_samples` throw an `audio_error` for 0 or more than 64 channels or a sample rate of 0 or less; `load` throws one when the file can't be read.

### Saving

`save(path)` writes the buffer, in the format its extension names (`.wav`, `.flac` or `.mp3`); `save(path, FileType::flac)` picks the format, and `save(path, options)` takes a `SaveOptions`:

| Field | Default | Meaning |
|---|---|---|
| `format` | `FileType::detect` | The format; `detect` uses the extension |
| `wav_format` | `SampleFormat::s16` | The WAV sample format: `u8`, `s16`, `s24`, `s32`, `f32`, `f64`, `alaw` or `ulaw` |
| `dither` | `true` | Add a little noise when writing fewer bits, which sounds better than plain rounding |
| `flac_compression` | `5` | FLAC compression level, 0 (fastest) to 8 (smallest) |
| `flac_bits` | `16` | FLAC bits per sample |
| `mp3_bitrate` | `192` | MP3 bitrate in kbit/s (8 to 320) |
| `mp3_vbr`, `mp3_vbr_quality` | `false`, `2` | Variable bitrate MP3, quality 0 (best) to 9 |
| `mp3_quality` | `2` | How hard the MP3 encoder works, 0 (best) to 9 |

`tag(key, value)` adds a tag such as `"title"` or `"artist"` (written to MP3 and FLAC files) and returns the options, so tags chain: `SaveOptions(FileType::mp3).tag("title", "Intro").tag("artist", "Me")`.

```walnut
import module.audio

using std::audio::AudioBuffer;
using std::audio::SaveOptions;
using std::audio::SampleFormat;

function main() -> int {
    AudioBuffer: take = AudioBuffer::tone(220.0, 2.0, channels = 2);
    SaveOptions: studio;
    studio.wav_format = SampleFormat::s24;
    take.save("take.wav", studio);
    SaveOptions: small;
    small.mp3_bitrate = 128;
    small.tag("title", "Take 1");
    take.save("take.mp3", small);
    take.save("take.flac");
    return 0;
} end function main
```

`std::audio::file_info(path)` reads a file's header without decoding it and gives an `AudioFileInfo` with `format()` (a `FileType`), `format_name()`, `sample_rate()`, `channels()`, `frames()`, `duration()`, `bitrate_kbps()`, `tag(key)` and `tag_keys()`. `std::audio::format_of(path)` gives the `FileType` an extension names, and `readable_extensions()` and `writable_extensions()` list them.

For video files, `file_info` reports `FileType::mp4` or `FileType::matroska` (WebM is Matroska) and describes the main audio track. To play a video's sound, `load` it or use `Mixer::play_file`; for its pictures, tracks and codecs, see [`video`](video.md).

### Size and samples

| Method | Gives or does |
|---|---|
| `channels()`, `sample_rate()`, `frames()`, `sample_count()` | The format and length; `sample_count()` is `frames() * channels()` |
| `duration()` | The length in seconds |
| `empty()` | `true` when there are no samples |
| `sample(frame, channel = 0)` | One sample, as a `std::hardware_float` |
| `set_sample(frame, channel, value)` | Changes one sample; `value` can be a `std::hardware_float` or any Walnut float |
| `samples()`, `set_samples(values)` | All the samples, interleaved, as a `Vector<std::hardware_float>` |
| `channel(index)`, `set_channel(index, values)` | One channel's samples |
| `frame_at(seconds)`, `seconds_at(frame)` | Converts between time and frames |
| `set_sample_rate(rate)` | Changes the rate without changing the samples (so the sound plays faster or slower) |
| `resize(frames)`, `clear()`, `silence()` | Changes the length (new frames are silent), removes every frame, or makes every sample 0 |

`sample`, `set_sample`, `channel` and `set_channel` throw an `audio_error` outside the buffer.

### Editing

These change the buffer itself.

| Method | Does |
|---|---|
| `append(other)` | Adds `other` to the end |
| `insert(at_frame, other)` | Puts `other` in at a frame |
| `erase(start_frame, count)` | Removes frames |
| `mix(other, at_frame = 0, gain = 1.0)` | Adds `other`'s samples into this one, starting at a frame, growing the buffer if `other` runs past its end |
| `gain(factor)`, `gain_db(db)` | Makes it louder or quieter |
| `normalize(target_peak = 1.0)`, `normalize_db(target_db)` | Scales it so its loudest sample reaches the target |
| `normalize_loudness(target_lufs = -14.0, max_true_peak_db = -1.0)` | Scales it to a loudness, as streaming services do, without letting peaks pass `max_true_peak_db` |
| `clamp()` | Cuts samples back to -1.0 to 1.0 |
| `fade_in(seconds, curve = FadeCurve::linear)`, `fade_out(seconds, curve)` | Fades from or to silence at the start or end |
| `fade(start_frame, count, from_gain, to_gain, curve)` | Any fade over a part of the buffer |
| `gain_envelope(times, dbs)` | Changes the level over time: `times` (seconds) and `dbs` are `Vector<std::hardware_double>`s of the same length, and the level moves in straight lines between the points |
| `reverse()` | Plays it backwards |
| `invert_polarity()`, `remove_dc()` | Turns every sample's sign around, or removes a constant offset |
| `swap_channels(first = 0, second = 1)` | Swaps two channels, such as left and right |
| `pad(before_seconds, after_seconds)` | Adds silence at both ends |
| `trim_silence(threshold_db = -60.0, keep_ms = 10.0)` | Removes quiet parts at both ends, keeping a little |

`append`, `insert` and `mix` need buffers with the same channels and sample rate (except `append` on an empty buffer) and throw an `audio_error` otherwise; convert one first with `converted()`.

### New buffers from old ones

These leave the buffer alone and give a new one.

| Method | Gives |
|---|---|
| `slice(start_frame, count)` | A part, by frames; throws outside the buffer |
| `cut(start_seconds, length_seconds)` | A part, by time; the parts outside the buffer are left off |
| `resampled(rate, quality = Quality::high)` | The same sound at another sample rate |
| `with_channels(count)` | The same sound with more or fewer channels |
| `converted(channels, rate, quality = Quality::high)` | Both at once |
| `to_mono()`, `to_stereo()` | One or two channels |
| `time_stretched(tempo, quality, mode)` | Faster (`tempo` above 1) or slower without changing the pitch; `tempo` is 0.25 to 4 |
| `pitch_shifted(semitones, quality, mode)` | Higher or lower without changing the length; -24 to 24 semitones |
| `stretched(tempo, semitones, quality, mode)` | Both at once |
| `speed_changed(factor)` | Faster and higher (or slower and lower) together, like a tape |
| `applied(effect, keep_tail = true)`, `applied(chain, keep_tail = true)` | The buffer through an [effect](#effects) or a chain |

`std::audio::crossfade(first, second, seconds, curve = FadeCurve::equal_power)` joins two buffers, fading one into the other over `seconds`, and `std::audio::mix(first, second, gain = 1.0)` adds them. `std::audio::split_on_silence(buffer, threshold_db = -50.0, min_silence_seconds = 0.3)` cuts a recording into a `Vector<AudioBuffer>` at the quiet parts, and `buffer.silent_parts(threshold_db = -60.0, min_seconds = 0.1)` gives the quiet parts' frame ranges as pairs of numbers (start, end, start, end, ...).

`time_stretched`, `pitch_shifted` and `stretched` take a [`StretchQuality`](#enums) (`fast`, `normal`, `high`) and a [`StretchMode`](#enums) (`detect` looks at the sound and picks; `phase_vocoder` suits music and `wsola` suits speech and drums).

### Measuring

| Method | Gives |
|---|---|
| `peak()`, `rms()` | The loudest sample, and the average level, as `std::hardware_float`s from 0.0 to 1.0 |
| `peak_db()`, `true_peak_db()` | The peak in dBFS; the true peak also counts peaks between samples |
| `loudness()` | A `Loudness`, measured the way broadcasters and streaming services do (EBU R128): `integrated()` (LUFS), `range()` (LU), `true_peak_db()`, `sample_peak_db()`, `max_momentary()` and `max_short_term()` |
| `pitch(min_hz = 50.0, max_hz = 2000.0)` | The main frequency in Hz, or 0 when there isn't a clear one, such as for noise |
| `spectrum(bands = 32, min_hz = 20.0, max_hz = 20000.0, fft_size = 4096)` | The level in dB of each of `bands` frequency bands, spaced evenly on a log scale, as a `Vector<std::hardware_float>`; good for drawing a spectrum |

`std::audio::db_to_gain(db)` and `std::audio::gain_to_db(gain)` convert between decibels and multipliers: +6 dB is about twice as loud in amplitude.

## Effects

An `Effect` changes sound. Make one with a function below, set its parameters by name, and use it on a buffer (`buffer.applied(effect)`), in a [chain](#effect-chains-and-presets), on a [timeline track](#timeline) or [live](#livechain-microphone-to-speakers-through-effects).

```walnut
import module.audio

using std::audio::AudioBuffer;
using std::audio::Effect;

function main() -> int {
    AudioBuffer: voice = AudioBuffer::load("voice.wav");
    Effect: lower = Effect::pitch_shift(-4.0);
    lower.set("preserve_formants", 1.0);
    Effect: squash = Effect::compressor(threshold_db = -24.0, ratio = 3.0);
    AudioBuffer: done = voice.applied(lower).applied(squash);
    done.save("deeper.wav");
    return 0;
} end function main
```

An `Effect` is a handle: copying one gives another name for the same effect, so changing a parameter through either changes it everywhere it's used, even while it's playing. `clone()` makes an independent copy.

### Making effects

| Function | Effect and parameters (with their ranges) |
|---|---|
| `Effect::gain(db = 0.0)` | Volume. `db` -120 to 48 |
| `Effect::filter(type, frequency, q = 0.707, gain_db = 0.0, slope_db = 12.0)` | One filter of a [`FilterType`](#enums). `type` 0 to 7, `frequency` 10 to 24000 Hz, `q` 0.025 to 40, `gain` -36 to 36 dB (for `peak` and the shelves), `slope` 12 to 48 dB/octave |
| `Effect::lowpass(frequency, q)`, `highpass(frequency, q)`, `bandpass(frequency, q)`, `notch(frequency, q)` | The common filters |
| `Effect::eq(bands = 8)` | A parametric EQ with up to 16 bands, all off at first. Set a band with `set_band(index, type, frequency, q = 1.0, gain_db = 0.0, on = true)` (bands count from 0), or by name: `band1_enabled`, `band1_type`, `band1_frequency`, `band1_q`, `band1_gain`, ... (names count from 1), and `output` |
| `Effect::graphic_eq()` | A 10-band graphic EQ at 31 Hz to 16 kHz; `set_band_gain(index, db)` (-12 to 12), or the parameters `31Hz`, `63Hz`, `125Hz`, ..., `16kHz`, and `output` |
| `Effect::dc_blocker()` | Removes a constant offset. `frequency` |
| `Effect::compressor(threshold_db = -18.0, ratio = 4.0, attack_ms = 10.0, release_ms = 100.0)` | Evens out the level. `threshold`, `ratio` (1 to 100), `knee`, `attack`, `release`, `makeup`, `auto_makeup`, `detection` (0 peak, 1 RMS), `link`, `lookahead`, `sidechain_hpf` |
| `Effect::limiter(ceiling_db = -1.0, release_ms = 50.0, lookahead_ms = 5.0)` | Keeps peaks under the ceiling. `ceiling`, `release`, `lookahead`, `true_peak` |
| `Effect::gate(threshold_db = -40.0, attack_ms = 1.0, hold_ms = 50.0, release_ms = 100.0)` | Silences everything quieter than the threshold, such as background noise between words. `threshold`, `hysteresis`, `attack`, `hold`, `release`, `range` |
| `Effect::expander(threshold_db = -40.0, ratio = 2.0)` | A gentler gate. `threshold`, `ratio`, `attack`, `release`, `range` |
| `Effect::de_esser(frequency = 6000.0, threshold_db = -30.0, amount_db = 6.0)` | Tames sharp "s" sounds. `frequency`, `threshold`, `amount` |
| `Effect::reverb()` | A room. `size` (0 to 1), `decay` (0.1 to 30 s), `damping`, `predelay_ms`, `diffusion`, `width`, `mix` |
| `Effect::delay()` | Echoes. `time_ms`, `sync` (1 to follow `bpm` and `note`), `bpm`, `note`, `feedback`, `pingpong`, `low_cut`, `high_cut`, `mix` |
| `Effect::chorus()`, `flanger()`, `phaser()` | Sweeping effects. `rate`, `depth_ms` or `depth`, `delay_ms`, `voices` (chorus), `stages`, `min_hz`, `max_hz` (phaser), `feedback`, `mix`, `spread`, `shape` |
| `Effect::tremolo()`, `vibrato()`, `autopan()` | Wobbling volume, pitch or position. `rate`, `depth` or `depth_ms`, `spread`, `shape`, `mix` (vibrato) |
| `Effect::distortion()` | Overdrive. `mode` (0 soft, 1 hard, 2 tube, 3 fold), `drive`, `tone`, `mix`, `output`, `oversample` |
| `Effect::bitcrusher()` | Lo-fi. `bits` (1 to 24), `rate_hz`, `mix` |
| `Effect::pitch_shift(semitones)` | Higher or lower voice, live. `semitones` (-24 to 24), `cents`, `mode`, `preserve_formants`, `formant` |
| `Effect::formant_shift(semitones)` | Changes how big a voice sounds without changing its pitch. `shift` (-12 to 12) |
| `Effect::autotune(key = 0, scale = Scale::chromatic)` | Pulls singing to the nearest note of a key (`key` 0 is C, 1 C#, ... 11 B) and [`Scale`](#enums). `key`, `scale`, `mask`, `retune_ms` (0 is the hard, robotic sound), `humanize`, `preserve_formants`, `min_hz`, `max_hz`, `reference_hz` |
| `Effect::voice_changer(preset = VoicePreset::none)` | Ready-made voices: a [`VoicePreset`](#enums) such as `helium`, `deep`, `robot`, `alien`, `monster`, `radio`, `telephone` or `cave`. `preset` (changing it crossfades to the new voice), `mix`, `output_gain` |
| `Effect::voice_eq()` | EQ shaped for voices. `low_cut`, `high_cut`, `peak_hz`, `peak_db`, `peak_q` |
| `Effect::ring_modulator()` | A metallic, robotic sound. `frequency`, `shape`, `mix` |
| `Effect::vocoder()` | The classic synth voice. `carrier`, `pitch`, `bands`, `attack_ms`, `release_ms`, `sibilance`, `shift`, `mix` |
| `Effect::noise_reduction()` | Removes steady noise such as hiss or fans. Give it a recording of just the noise with `learn_noise(buffer)` for the best result; without one it learns as it goes. `mode`, `reduction_db`, `sensitivity`, `frequency_smoothing`, `time_smoothing`, `per_channel` |
| `Effect::hum_remover(frequency = 0.0)` | Removes mains hum (50 or 60 Hz and its harmonics); 0 finds the frequency itself. `frequency`, `harmonics`, `width` |
| `Effect::click_remover()` | Repairs clicks and pops. `sensitivity`, `max_width` |
| `Effect::create(type)` | An effect by its type name, such as `"reverb"`; `Effect::types()` lists them |
| `Effect::custom(body, name = "custom")` | Your own effect, written in Walnut; see [below](#custom-effects) |

### Using an effect

| Method | Gives or does |
|---|---|
| `set(name, value)`, `get(name)` | A parameter's value. Values outside the range are moved to the nearest end. An unknown name throws an `audio_error` that lists the names the effect has |
| `has(name)`, `parameter_names()` | Whether a parameter exists, and all their names |
| `minimum(name)`, `maximum(name)`, `unit(name)` | A parameter's range, and its unit, such as `"dB"` or `"Hz"` |
| `name()`, `type()` | What kind of effect it is |
| `set_enabled(on)`, `enabled()` | Turns it off (bypasses it) and on again |
| `process(buffer, keep_tail = true)` | The buffer through the effect, the same as `buffer.applied(effect)` |
| `apply_to(buffer, keep_tail = true)` | Changes the buffer itself, the same as `buffer.apply(effect)` |
| `reset()` | Forgets the sound it has seen, such as a reverb's tail |
| `latency_frames()`, `tail_frames()` | How late the effect's output is, and how long it keeps sounding after the input stops, once it knows the sample rate |
| `detected_pitch()` | For `pitch_shift` and `autotune`, the pitch heard last, in Hz |
| `gain_reduction_db()` | For the compressor, limiter, gate, expander and de-esser, how much it's turning down right now |
| `learn_noise(buffer)` | For `noise_reduction`, a recording of just the noise |
| `clone()` | An independent copy with the same settings (a custom effect gives itself back, since its function can't be copied) |
| `to_json()`, `Effect::from_json(text)` | Saves the effect's settings as text and makes one back from it |

Working on a buffer, an effect is given the buffer's sample rate and channels first, and its delay (`latency_frames()`) is taken off, so the result lines up with the input. With `keep_tail = true`, the result is longer by `tail_frames()` so a reverb or delay can ring out; with `false`, it's as long as the input.

### Custom effects

`Effect::custom(body)` makes an effect from a Walnut function that gets an [`AudioBlock`](#audioblock), a short piece of the sound, and changes it in place:

```walnut
import module.audio

using std::audio::AudioBlock;
using std::audio::AudioBuffer;
using std::audio::Effect;

function main() -> int {
    Effect: fuzz = Effect::custom([](AudioBlock&: block) -> void {
        for (long int: i = 0; i < block.sample_count(); i++) {
            std::hardware_float: s = block.sample(i) * 4.0;
            if (s > 0.5) then {
                s = 0.5;
            }
            if (s < -0.5) then {
                s = -0.5;
            }
            block.set_sample(i, s);
        }
    }, "fuzz");
    AudioBuffer: guitar = AudioBuffer::tone(110.0, 2.0, shape = std::audio::Waveform::saw);
    guitar.applied(fuzz).save("fuzz.wav");
    return 0;
} end function main
```

A custom effect works everywhere a built-in one does, including in live chains, where its function runs on the [audio thread](#the-audio-thread). If its function throws, it goes silent and keeps the message: `error()` gives it, and `clear_error()` lets it run again.

## Effect chains and presets

An `EffectChain` runs effects one after another. Like an `Effect`, it's a handle, and it can be changed while it plays: adding, removing or moving an effect takes effect at the start of the next block of sound, without a click.

```walnut
import module.audio

using std::audio::AudioBuffer;
using std::audio::Effect;
using std::audio::EffectChain;

function main() -> int {
    EffectChain: podcast = EffectChain(Effect::highpass(80.0), Effect::noise_reduction(), Effect::compressor(-20.0, 3.0), Effect::limiter(-1.0));
    podcast.at(1).set("reduction_db", 12.0);
    podcast.save("podcast.json", "Podcast voice", "me", "Clean up a spoken track");
    EffectChain: same = EffectChain::load("podcast.json");
    AudioBuffer::load("interview.wav").applied(same).save("interview-clean.wav");
    return 0;
} end function main
```

| Written | Gives or does |
|---|---|
| `EffectChain()`, `EffectChain(effects...)` | An empty chain, or one with these effects in order |
| `add(effect)` | Adds an effect at the end and returns the chain, so `chain.add(a).add(b)` works |
| `insert(index, effect)`, `remove(index)`, `move(from_index, to_index)`, `clear()` | Changes the order |
| `size()`, `at(index)`, `chain[index]` | The effects; `at` and `[]` throw an `audio_error` for an index outside the chain |
| `set_enabled(on)`, `reset()`, `latency_frames()`, `tail_frames()` | As for an `Effect`, for the whole chain |
| `process(buffer, keep_tail = true)`, `apply_to(buffer, keep_tail = true)` | Runs a buffer through the chain |
| `as_effect()` | The chain as an `Effect`, so it can go inside another chain |
| `EffectChain::voice_preset(preset)` | One of the built-in voices as a chain, from a [`VoicePreset`](#enums) or its name (`"robot"`); `EffectChain::voice_preset_names()` lists them |

**Presets.** `save(path, name = "", author = "", description = "")` writes the chain and every setting to a JSON file, and `EffectChain::load(path)` reads one back; `to_json(name = "")` and `EffectChain::from_json(text)` do the same with text. A preset that names an effect this version doesn't know still loads, with a stand-in that lets the sound through unchanged and keeps its settings, so saving it again loses nothing. A custom effect's code can't be saved, so it comes back as such a stand-in.

## Playing sounds: Mixer

`std::audio::play(buffer)` plays a buffer through the default speakers and waits until it ends, and `std::audio::play_file(path)` does the same with a file. For anything more, use a `Mixer`: it plays any number of sounds at once, each with its own volume, position and speed, and they can be started, changed and stopped at any time, as a soundboard or a game does.

```walnut
import module.audio
import module.io
import module.threading

using std::audio::Mixer;
using std::audio::Sound;
using std::audio::Voice;

function main() -> int {
    Mixer: board;
    board.start();
    Sound: horn = Sound::load("horn.wav");
    Sound: drum = Sound::load("drum.wav");
    Voice: music = board.play_file("music.mp3", gain = 0.4, loop = true, fade_in = 2.0);
    board.play(horn, pan = -0.5);
    std::threading::this_thread::sleep_for(500);
    board.play(drum, speed = 1.5, gain = 0.8);
    std::threading::this_thread::sleep_for(3000);
    music.stop(fade_out_seconds = 1.5);
    board.wait_idle();
    return 0;
} end function main
```

**Sound.** A `Sound` is a buffer ready to play: `Sound(buffer)` or `Sound::load(path)`. Playing it many times shares its samples instead of copying them. It has `valid()`, `channels()`, `sample_rate()`, `frames()`, `duration()` and `to_buffer()`.

**Mixer.** `Mixer()` mixes at 48000 Hz in stereo, and `Mixer(sample_rate, channels = 2)` picks; sounds of other rates and channel counts are converted as they play.

| Method | Does |
|---|---|
| `play(sound, ...)`, `play(buffer, ...)` | Starts a sound and gives its [`Voice`](#voice) |
| `play_file(path, ...)` | Plays a file while it's being read, without loading it all first; for long music |
| `stop_all(fade_out_seconds = 0.0)` | Stops everything |
| `voice_count()` | How many sounds are playing |
| `set_master_gain(gain)`, `master_gain()`, `output_peak()` | The volume of everything together, and the loudest recent sample |
| `effects()` | The `EffectChain` the whole mix goes through, such as a limiter |
| `start(device = "", latency_ms = 30)` | Plays through a device (the default output when `device` is `""`); throws an `audio_error` when it can't be opened |
| `stop_output()`, `output_running()`, `output_failed()`, `output_error()`, `device_name()` | The device |
| `wait_idle(timeout_ms = -1)` | Waits until nothing is playing (-1 waits as long as it takes); `false` if it gave up or the device stopped |
| `render(frames)`, `render_seconds(seconds)` | Mixes the next stretch of sound into a buffer instead of a device, to save a mix or test without a sound card. Throws while `start()`ed |

`play`, `play_buffer` and `play_file` all take these, named or in order:

| Parameter | Default | Meaning |
|---|---|---|
| `gain` | `1.0` | Volume |
| `pan` | `0.0` | -1.0 is left, 1.0 is right |
| `speed` | `1.0` | 2.0 plays twice as fast (and an octave higher, unless `preserve_pitch`) |
| `loop` | `false` | Starts again at the end |
| `paused` | `false` | Starts paused, to `resume()` later |
| `start` | `0.0` | Where in the sound to start, in seconds |
| `fade_in` | `0.0` | Seconds to fade in |
| `pitch` | `0.0` | Semitones up or down, without changing the speed |
| `preserve_pitch` | `false` | Keeps the pitch when `speed` changes |

### Voice

A `Voice` is one sound playing on a mixer.

| Method | Does |
|---|---|
| `is_playing()`, `is_paused()` | Whether it's still going |
| `stop(fade_out_seconds = 0.0)` | Stops it |
| `pause()`, `resume()` | Pauses and goes on |
| `set_gain(gain)`, `set_pan(pan)`, `set_speed(speed)`, `set_loop(on)` | Changes it while it plays |
| `set_pitch(semitones)`, `set_preserve_pitch(on)` | Changes the pitch while it plays |
| `seek(seconds)`, `position()`, `duration()` | Where it is |
| `wait(timeout_ms = -1)` | Waits until it ends; `false` if it gave up first, or straight away when the mixer isn't playing through a device (use `render` then) |
| `id()`, `valid()` | A number for the voice, and whether it's a real one |

A voice that has ended (or whose mixer is gone) ignores changes, and `is_playing()` is `false`.

## Devices

| Function | Gives or does |
|---|---|
| `std::audio::devices()`, `devices(kind)` | The sound devices, as a `Vector<Device>`; `kind` is a [`DeviceKind`](#enums): `output`, `input` or `loopback` (what another output is playing, to record the computer's own sound) |
| `std::audio::default_device(kind = DeviceKind::output)` | The system's default device of that kind, as an [`Optional<Device>`](optional.md) |
| `std::audio::backend()` | The sound system in use: `"wasapi"`, `"pulse"`, `"alsa"`, `"coreaudio"`, `"aaudio"` or `"null"` |
| `std::audio::backends()` | The ones available here |
| `std::audio::use_backend(name)` | Switches to another one; throws an `audio_error` if it isn't available |

A `Device` has `id()` (what to pass as `device` when opening one), `name()` (to show people), `kind()`, `is_default()`, `sample_rate()`, `channels()`, `backend()` and `to_string()`.

```walnut
import module.audio
import module.io

function main() -> int {
    std::io::println("using ", std::audio::backend());
    for (const std::audio::Device&: d in std::audio::devices(std::audio::DeviceKind::input)) {
        std::io::println(d.name(), " (", d.id(), ")");
    }
    return 0;
} end function main
```

## Streams: your own code on the audio thread

A `Stream` calls a Walnut function every time the sound card needs more sound (or has recorded some), with an `AudioBlock` holding a few milliseconds of it. This is for making sound yourself, such as a synthesizer, or for reading a microphone as it records.

```walnut
import module.audio
import module.threading

using std::audio::AudioBlock;
using std::audio::Stream;

function main() -> int {
    std::hardware_double: phase = 0.0;
    std::hardware_double: frequency = 220.0;
    Stream: synth = Stream::playback([&](AudioBlock&: block) -> void {
        const std::hardware_double: step = frequency / static_cast<std::hardware_double>(block.sample_rate());
        for (long int: f = 0; f < block.frames(); f++) {
            std::hardware_float: s = 0.2;
            if (phase >= 0.5) then {
                s = -0.2;
            }
            for (long int: c = 0; c < block.channels(); c++) {
                block.set(f, c, s);
            }
            phase += step;
            if (phase >= 1.0) then {
                phase -= 1.0;
            }
        }
    });
    synth.start();
    std::threading::this_thread::sleep_for(1000);
    frequency = 330.0;
    std::threading::this_thread::sleep_for(1000);
    synth.close();
    return 0;
} end function main
```

This plays a square wave for a second, then a higher one for another second.

| Function | Opens |
|---|---|
| `Stream::playback(callback, device = "", sample_rate = 0, channels = 0, latency_ms = 20, name = "walnut")` | An output; `callback` is a `function(AudioBlock&) -> void` that fills the block |
| `Stream::capture(callback, ...)` | An input such as a microphone; `callback` is a `function(const AudioBlock&) -> void` that reads it |
| `Stream::loopback(callback, ...)` | What an output is playing, the same way |

A `device` of `""` is the default, and a `sample_rate` or `channels` of 0 is the device's own. They throw an `audio_error` when the device can't be opened. A stream is opened stopped:

| Method | Does |
|---|---|
| `start()`, `stop()` | Starts and pauses the callbacks; `start()` throws an `audio_error` if the device won't start |
| `close()` | Stops and lets the device go. Going out of scope does the same |
| `is_open()`, `running()` | Its state |
| `failed()`, `error()`, `check()` | Whether the callback threw or the device stopped working, the message, and `check()`, which throws it as an `audio_error` |
| `sample_rate()`, `channels()`, `latency_frames()`, `device_name()`, `kind()` | What was opened |
| `frames_processed()`, `seconds_processed()` | How much sound has gone through |

A `Stream` can't be copied, but it can be moved.

### AudioBlock

| Method | Gives or does |
|---|---|
| `frames()`, `channels()`, `sample_rate()`, `sample_count()` | The block's size and format |
| `get(frame, channel = 0)`, `set(frame, channel, value)` | One sample |
| `sample(index)`, `set_sample(index, value)` | One sample by its place in the interleaved samples, from 0 to `sample_count() - 1` |
| `fill(value)`, `silence()`, `gain(factor)` | The whole block |
| `peak()`, `rms()` | Its level |
| `copy_from(buffer, start_frame = 0, gain = 1.0)`, `add_from(buffer, start_frame = 0, gain = 1.0)` | Copies or mixes a buffer's frames in, starting at `start_frame` of the buffer, and gives how many frames that was; mono buffers go to every channel |
| `to_buffer()` | A copy of the block as an `AudioBuffer` |
| `writable()` | `false` for a capture block, which can only be read |

Samples set with a `std::hardware_float` go in directly; other floats are converted. Reaching outside the block, or writing to a capture block, stops the stream with an error (see `failed()`).

### The audio thread

The callback doesn't run on the thread that opened the stream but on the sound card's own audio thread, while the rest of the program goes on. So:
- **Keep it quick.** The sound card waits for the callback; if it takes longer than the block lasts (a few milliseconds), the sound breaks up. Use `std::hardware_float` and `std::hardware_double` for the sound itself: Walnut's own floats are exact but much slower. Avoid printing, reading files, waiting on locks and making big objects in the callback.
- **Share data safely.** The callback and the rest of the program run at the same time. Plain numbers that one side only reads, like `frequency` above, are fine; for anything more, use `std::threading::Atomic` or a `Mutex` (briefly), or hand over whole buffers with `Sound`s and a `Mixer` instead.
- **Keep what the callback uses alive.** A `[&]` lambda refers to the variables around it, so declare the stream after them (it closes first when they all go out of scope) or `close()` it before they go away.
- **Errors stop the stream.** An exception the callback doesn't catch, or a runtime error such as reaching outside the block, makes the stream play silence and remember the message; check with `failed()` or `check()`.
- **Safety checks and `--gc` work on the audio thread.** While a stream, mixer, live chain or timeline player is running through a device, garbage collection under [`--gc`](../../documentation.md#safety-checks) waits, so it never frees memory the callback is still using; it catches up after the stream closes.

The same goes for [custom effects](#custom-effects) in a live chain, a mixer's effects or a timeline's.

## LiveChain: microphone to speakers through effects

A `LiveChain` records from an input, runs the sound through an `EffectChain` and plays it on an output, with as little delay as the devices allow. It's the heart of a voice changer:

```walnut
import module.audio
import module.io

using std::audio::Effect;
using std::audio::EffectChain;
using std::audio::LiveChain;
using std::audio::VoicePreset;

function main() -> int {
    LiveChain: live;
    live.effects().add(Effect::gate(-45.0));
    Effect: voice = Effect::voice_changer(VoicePreset::robot);
    live.effects().add(voice);
    live.effects().add(Effect::limiter(-1.0));
    live.start();
    std::io::println("talking through ", live.output_device(), " with ", live.latency_ms(), " ms of delay");
    std::io::println("press enter for the next voice");
    std::io::input_string("");
    voice.set("preset", static_cast<float>(static_cast<long int>(VoicePreset::helium)));
    std::io::input_string("");
    live.set_effects(EffectChain::voice_preset("monster"));
    std::io::input_string("");
    live.stop();
    return 0;
} end function main
```

To send the changed voice into a call or game, pick a virtual audio device as the output (on Windows a virtual cable driver; on Linux a PulseAudio null sink, whose monitor the other program records).

| Method | Does |
|---|---|
| `start(input = "", output = "", sample_rate = 0, channels = 0, latency_ms = 20, loopback = false)` | Opens the devices (the defaults when `""`) and starts. With `loopback = true`, `input` is a loopback device, to change the computer's own sound. Throws an `audio_error` when a device can't be opened |
| `stop()`, `running()` | Stops and closes both devices |
| `effects()` | The chain the sound goes through; change it at any time |
| `set_effects(chain)` | Replaces the chain's effects with another chain's, such as a loaded preset |
| `set_gain(gain)`, `gain()`, `set_muted(on)`, `muted()`, `set_bypass(on)`, `bypass()` | Output volume, muting, and turning the effects off without stopping |
| `input_peak()`, `output_peak()` | The loudest sample since the last call, for a level meter |
| `latency_ms()` | About how far the output is behind the input |
| `underruns()`, `overruns()`, `dropped_frames()` | Glitches: the output ran out of sound, the input had nowhere to go, or old sound was skipped to keep the delay down |
| `failed()`, `error()`, `check()` | As for a [`Stream`](#streams-your-own-code-on-the-audio-thread) |
| `sample_rate()`, `channels()`, `input_device()`, `output_device()`, `frames_processed()` | What's running |

The input is converted to the output's sample rate and channels, so any microphone works with any speakers. A `LiveChain` can't be copied.

## Timeline

A `Timeline` holds tracks, each with clips (sounds placed at a time), its own volume, pan and effects, and automation (volume, pan, mute or any effect parameter changing over time). It renders to a buffer or a file, or plays through a `TimelinePlayer`.

```walnut
import module.audio

using std::audio::AudioBuffer;
using std::audio::Effect;
using std::audio::Sound;
using std::audio::Timeline;
using std::audio::Track;

function main() -> int {
    Timeline: song(48000, 2);
    song.set_tempo(100.0);
    Track: drums = song.add_track("drums");
    Track: vocals = song.add_track("vocals");
    Sound: kick = Sound::load("kick.wav");
    for (int: beat = 0; beat < 16; beat++) {
        drums.add_clip(kick, song.beats_to_seconds(static_cast<float>(beat)));
    }
    long int: take = vocals.add_clip(AudioBuffer::load("vocal.wav"), song.bars_to_seconds(1.0), "verse");
    vocals.set_clip_fades(take, 0.05, 0.5);
    vocals.effects().add(Effect::compressor(-20.0, 3.0));
    vocals.effects().add(Effect::reverb());
    vocals.automate_effect(1, "mix", 0.0, 0.1);
    vocals.automate_effect(1, "mix", 8.0, 0.4);
    drums.automate_volume(0.0, -20.0);
    drums.automate_volume(2.0, 0.0);
    song.master_effects().add(Effect::limiter(-1.0));
    song.export("song.mp3");
    return 0;
} end function main
```

**Timeline**

| Method | Does |
|---|---|
| `Timeline()`, `Timeline(sample_rate, channels = 2)` | A new, empty timeline (48000 Hz stereo by default) |
| `add_track(name = "")`, `track(index)`, `track_count()`, `remove_track(index)` | The tracks |
| `length()` | Where the last clip ends, in seconds |
| `set_tempo(bpm)`, `tempo()`, `beats_to_seconds(beats)`, `bars_to_seconds(bars)`, `snap(seconds, division_beats = 1.0)` | Musical time |
| `add_marker(time, name = "")`, `marker_count()`, `marker_time(index)`, `marker_name(index)` | Markers |
| `set_master_volume_db(db)`, `master_volume_db()`, `master_effects()`, `automate_master_volume(time, db, curve)` | The master bus everything goes through |
| `set_loop(start, end_time, on = true)` | A loop range for the player |
| `render(include_tail = true)` | The whole song as a buffer; with the tail, reverbs and delays ring out at the end |
| `render(start, end_time, include_tail = false)` | Part of it |
| `export(path, options = SaveOptions())` | Renders and saves, the same as `render().save(path, options)` |

**Track.** A `Track` is a handle to one track of its timeline. Removing a track makes the handles of it and later tracks point to the tracks after them; get them again with `track(index)`.

| Method | Does |
|---|---|
| `name()`, `set_name(name)`, `index()` | Its name and place |
| `set_volume_db(db)`, `set_pan(pan)`, `set_mute(on)`, `set_solo(on)` (and the getters) | Its mixer settings |
| `add_clip(sound_or_buffer, position = 0.0, name = "")` | Places a sound at a time and gives the clip's index. Clips of the same `Sound` share its samples; a buffer is copied |
| `clip_count()`, `clip_position(i)`, `clip_length(i)`, `clip_end(i)`, `clip_name(i)` | The clips |
| `move_clip(i, time)`, `trim_clip_start(i, time)`, `trim_clip_end(i, time)`, `split_clip(i, time)`, `remove_clip(i)` | Editing clips |
| `set_clip_gain_db(i, db)`, `set_clip_muted(i, on)`, `set_clip_fades(i, fade_in, fade_out, curve = FadeCurve::linear)` | A clip's level and fades |
| `effects()` | The track's `EffectChain` |
| `automate_volume(time, db, curve)`, `automate_pan(time, pan, curve)`, `automate_mute(time, muted)` | Adds automation points; between points the value follows `curve`, an [`AutomationCurve`](#enums) |
| `automate_effect(effect_index, parameter, time, value, curve)` | Automates a parameter of one of the track's effects; throws an `audio_error` for an effect or parameter it doesn't have |
| `clear_automation()` | Removes all of the track's automation |
| `length()` | Where its last clip ends |

**TimelinePlayer.** Plays a timeline through a device, like a DAW's transport. `TimelinePlayer(sample_rate = 48000, channels = 2)`; `update(timeline)` gives it the current version of the timeline (call it again after changes; the player switches over smoothly); `play()`, `pause()`, `stop()`, `seek(seconds)`, `position()`, `is_playing()`, `set_loop(start, end_time, on = true)`; `start(device = "", latency_ms = 30)` and `stop_output()` for the device, and `render(frames)` to play into a buffer instead.

## Enums

| Enum | Values |
|---|---|
| `DeviceKind` | `output`, `input`, `loopback` |
| `FileType` | `detect`, `wav`, `flac`, `mp3`, `ogg_vorbis`, `ogg_opus`, `aac`, `mp4`, `matroska`, `ogg` (all of them load; only `wav`, `flac` and `mp3` can be saved) |
| `SampleFormat` | `u8`, `s16`, `s24`, `s32`, `f32`, `f64`, `alaw`, `ulaw` |
| `Quality` | `fast`, `medium`, `high`, `best` (for resampling) |
| `StretchQuality` | `fast`, `normal`, `high` |
| `StretchMode` | `detect`, `phase_vocoder`, `wsola` |
| `FadeCurve` | `linear`, `equal_power`, `logarithmic`, `exponential`, `s_curve` |
| `AutomationCurve` | `linear`, `hold`, `exponential`, `s_curve`, `bezier` |
| `FilterType` | `lowpass`, `highpass`, `bandpass`, `notch`, `peak`, `lowshelf`, `highshelf`, `allpass` |
| `Waveform` | `sine`, `square`, `saw`, `triangle`, `noise` |
| `VoicePreset` | `none`, `helium`, `deep`, `robot`, `alien`, `monster`, `radio`, `telephone`, `megaphone`, `cave`, `underwater`, `whisper`, `feminine`, `masculine`; `std::audio::voice_preset_name(preset)` gives the name |
| `Scale` | `chromatic`, `major`, `minor`, `harmonic_minor`, `pentatonic_major`, `pentatonic_minor`, `blues` |

Effect parameters that pick from a list, such as a filter's `type` or the voice changer's `preset`, take the enum's number: `voice.set("preset", static_cast<float>(static_cast<long int>(VoicePreset::helium)))`.

## Errors

Loading and saving files, opening devices, reading outside a buffer, unknown effect parameters and the other mistakes listed above throw a `std::errors::audio_error`. Its `what()` (or its `message` field) says what went wrong:

```walnut
import module.audio
import module.io

function main() -> int {
    try {
        std::audio::AudioBuffer: song = std::audio::AudioBuffer::load("missing.mp3");
    } catch (const std::errors::audio_error&: e) {
        std::io::println("problem: ", e.what());
    }
    return 0;
} end function main
```

This prints `problem: can't load 'missing.mp3': ` followed by the reason.

## Systems and backends

| System | Sound system used |
|---|---|
| Windows | WASAPI (Windows' own); loopback records any output |
| Linux | PulseAudio (and PipeWire, through its PulseAudio support) when it's running, otherwise ALSA; loopback uses PulseAudio's monitor devices |
| macOS | Core Audio. Microphones need `NSMicrophoneUsageDescription` in the app's `Info.plist`; loopback (the system's sound, through Core Audio process taps) needs macOS 14.2 or newer and `NSAudioCaptureUsageDescription` |
| Android | AAudio (Android 8.0, API 26, or newer). Microphones need the `RECORD_AUDIO` permission; loopback needs Android 10 (API 29) and a `MediaProjection` the user agreed to, handed to Fizmo by the app |

Nothing extra has to be installed to build a program: it loads the sound system when it runs. On a computer with no working sound system, opening a device throws an `audio_error` that says why, while buffers, effects, files, `Mixer::render` and timelines keep working. The `"null"` backend has one silent output and one silent input that run on a timer, for tests and servers without sound; the environment variable `FIZMO_AUDIO_BACKEND` (`pulse`, `alsa`, `wasapi`, `coreaudio`, `aaudio` or `null`) picks the backend before the program starts, and `std::audio::use_backend(name)` switches from inside the program.
