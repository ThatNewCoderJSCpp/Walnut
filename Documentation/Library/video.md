# video

`video` reads and plays video files. `probe` tells you what a file holds (its container, length, tracks, codecs, size, frame rate and tags) without decoding it; `extract_frame` gives one picture from a given time, as a thumbnail maker does; `Player` plays a file with its sound, hands your program each new frame as it's due, and seeks, loops, changes speed and reports what happens as events; and `Demuxer`, `VideoDecoder` and `H264Decoder` give the packets and frames themselves, for programs that want to do their own decoding or processing.

It's Fizmo's video library, which has its own MP4, MOV, Matroska, WebM, Ogg, IVF, AV1 (`.obu` and Annex B) and raw H.265 readers and its own H.264, H.265 (HEVC), AV1 and VP9 decoders, so programs need no other libraries. The sound goes through [`audio`](audio.md): AAC, Opus, Vorbis, MP3, FLAC and PCM tracks play, and a player can share an `audio::Mixer` with the rest of the program.

```walnut
import module.video
```

Everything is in the namespace `std::video`, and errors are [`std::errors::video_error`](#errors). `video` imports `audio`, `colors`, `collections` and `optional`.

## Contents

1. [A first example](#a-first-example)
2. [Codecs](#codecs)
3. [probe and MediaInfo](#probe-and-mediainfo)
4. [extract_frame, VideoFrame and Picture](#extract_frame-videoframe-and-picture)
5. [Player](#player)
6. [Frame loops](#frame-loops)
7. [Events](#events)
8. [Sharing a Mixer](#sharing-a-mixer)
9. [Demuxer: packets](#demuxer-packets)
10. [VideoDecoder and H264Decoder](#videodecoder-and-h264decoder)
11. [Sound from video files](#sound-from-video-files)
12. [Enums](#enums)
13. [Errors](#errors)

## A first example

```walnut
import module.video
import module.io

function main() -> int {
    std::video::MediaInfo: info = std::video::probe("holiday.mp4");
    std::io::println(info.width(), "x", info.height(), ", ", info.duration(), " s at ", info.frame_rate(), " fps");
    for (const std::video::Track&: t in info.tracks()) {
        std::io::println("  ", t);
    }
    std::video::VideoFrame: middle = std::video::extract_frame("holiday.mp4", info.duration() / 2.0);
    middle.save("thumbnail.jpg");
    std::io::println("the middle frame's top-left pixel is ", middle.pixel(0, 0));
    return 0;
} end function main
```

This prints something like `1280x720, 12.4 s at 30 fps`, one line per track (`0: video H.264 (AVC) 1280x720 at 30 fps`, `1: audio aac 48000 Hz, 2 channels`), saves the middle frame as a JPEG and prints the color of its corner.

## Codecs

| Kind | Can be read |
|---|---|
| Containers | MP4, MOV, M4V and 3GP; Matroska (MKV) and WebM; Ogg; IVF (VP9 or AV1); AV1 low-overhead `.obu` and Annex B streams; raw H.265 (`.hevc`, `.h265`) streams |
| Video | H.264 (AVC): Baseline, Main, High, High 10, High 4:2:2 and High 4:4:4 (with lossless) profiles, 8 to 14 bits, grayscale, 4:2:0, 4:2:2 or 4:4:4, progressive or interlaced (field pictures and MBAFF), with CAVLC or CABAC and B-frames |
| | H.265 (HEVC): Main, Main 10, Main 12 and the range extensions (grayscale, 4:2:2 and 4:4:4 up to 12 bits, lossless), with tiles, wavefronts and B-frames |
| | AV1: Main, High and Professional profiles, 8, 10 and 12 bits, grayscale, 4:2:0, 4:2:2 or 4:4:4, with film grain |
| | VP9: profiles 0 to 3, 8, 10 and 12 bits, 4:2:0, 4:2:2, 4:4:0 or 4:4:4 |
| Audio | AAC (with SBR and PS), Opus, Vorbis, MP3, FLAC and PCM |

H.264 streams that use slice groups (FMO), data partitioning or SP/SI slices (the Extended profile) don't decode: the decoder reports an error for each picture. Interlaced video decodes into frames that hold both fields; a `Player` deinterlaces them as it plays (see [`PlayerOptions`](#playeroptions)). Other video codecs, such as VP8, Theora and MPEG-4 Part 2, are recognized but not decoded: `probe` describes their tracks and sets `video_supported()` to `false` with a message in `video_error()`, `extract_frame` throws a `video_error` that names the codec, and a `Player` still plays the file's sound (with a `warning` event saying why there is no picture).

| Function | Gives |
|---|---|
| `std::video::supported_video_codecs()` | The video codecs that decode, as a `Vector<string>` of short names (`"h264"`, `"hevc"`, `"av1"`, `"vp9"`) |
| `std::video::video_codec_supported(codec)`, `audio_codec_supported(codec)`, `codec_supported(codec)` | Whether a codec (a short name such as `"h264"`, `"vp9"`, `"aac"` or `"opus"`, as `Track::codec()` gives) can be decoded |
| `std::video::codec_name(codec)` | A name to show people, such as `"H.264 (AVC)"` or `"H.265 (HEVC)"` |

## probe and MediaInfo

`std::video::probe(path)` reads a file's headers (it doesn't decode anything, so it's quick) and gives a `MediaInfo`. `probe(bytes)` does the same with a file already in memory, as a `Vector<std::hardware_uint8>`; `std::video::read_file(path)` reads a whole file into one. Both throw a `video_error` when the file can't be read or isn't a format they know. A file whose codecs aren't supported still probes.

| Method | Gives |
|---|---|
| `container()` | `"mp4"`, `"mov"`, `"m4a"`, `"3gp"`, `"matroska"`, `"webm"` or `"ogg"`; for a plain audio file, `"wav"`, `"flac"`, `"mp3"` or `"aac"` |
| `duration()` | The length in seconds |
| `tracks()`, `track(index)`, `track_count()` | The tracks, as [`Track`](#track)s; `track` throws a `video_error` for a track that doesn't exist |
| `video_track()`, `audio_track()` | The index of the track a `Player` would use, or -1 when there is none |
| `has_video()`, `has_audio()` | Whether there is such a track |
| `video_supported()`, `audio_supported()` | Whether it can be decoded |
| `video_codec()`, `audio_codec()` | Their codecs' short names |
| `video_error()`, `audio_error()` | Why a track can't be decoded, or `""` |
| `width()`, `height()` | The video's size in pixels |
| `display_width()`, `display_height()`, `display_aspect()` | The size and shape it should be shown at, for video with non-square pixels |
| `rotation()` | How far it should be turned when shown: 0, 90, 180 or 270 degrees clockwise, as phones record it |
| `frame_rate()` | Frames a second |
| `sample_rate()`, `channels()` | The sound's format |
| `tag(key)`, `tag_keys()` | Tags such as `"title"`, `"artist"` or `"encoder"`; `tag` ignores case and gives `""` for a missing tag |
| `error()`, `valid()` | Only of interest for a `MediaInfo` you didn't get from `probe` |
| `to_string()` | Such as `mp4, 2 s, 2 tracks` |

### Track

| Method | Gives |
|---|---|
| `index()`, `id()` | Its place in the file (what `Demuxer`, `Packet::track()` and `Player::select_video_track` use) and the container's own number for it |
| `kind()`, `kind_name()` | A [`MediaType`](#enums): `video`, `audio`, `subtitle` or `other`, and its name |
| `codec()`, `codec_name()`, `supported()` | The codec's short name, a longer name, and whether it decodes |
| `language()`, `name()`, `is_default()` | What the file says about it |
| `width()`, `height()`, `display_width()`, `display_height()`, `rotation()`, `frame_rate()`, `frame_count()` | For video |
| `sample_rate()`, `channels()`, `bits_per_sample()` | For audio |
| `duration()`, `bitrate()` | Its length in seconds and bits a second (0 when the file doesn't say) |
| `codec_config()` | The codec's setup data from the container (for H.264, the `avcC` record), for [`H264Decoder::open`](#videodecoder-and-h264decoder) |
| `to_string()` | Such as `0: video H.264 (AVC) 64x48 at 10 fps` |

## extract_frame, VideoFrame and Picture

`std::video::extract_frame(path, seconds = 0.0)` decodes the frame showing at that time (seeking to the keyframe before it and decoding forward, so it's exact) and gives a `VideoFrame`; `extract_frame(bytes, seconds)` works on a file in memory. They throw a `video_error` when the file can't be read, has no video, or its codec isn't supported.

A `VideoFrame` is one decoded picture as the codec made it: in Y'CbCr ("YUV"), with the color (chroma) planes usually at half the size of the brightness (luma) plane. It's a value, and copying one is cheap: copies share the pixels, which never change.

| Method | Gives |
|---|---|
| `width()`, `height()` | Its size |
| `pts()` | When it's shown, in seconds from the start |
| `keyframe()` | Whether it can be decoded on its own |
| `serial()` | For frames from a `Player`, its number (see [frame loops](#frame-loops)); 0 otherwise |
| `pixel(x, y)` | One pixel converted to RGB, as a [`Color`](colors.md); throws a `video_error` outside the frame |
| `rgba()` | The whole frame converted to RGBA: `width() * height() * 4` bytes, row by row from the top, as a `Vector<std::hardware_uint8>` |
| `to_picture()` | The frame converted to a [`Picture`](#picture) |
| `save(path, quality = 90)` | Writes it as an image; the extension picks the format: `.png`, `.jpg`, `.bmp`, `.gif`, `.tga`, `.qoi`, `.webp`, `.tiff` and the others the [`images`](images.md) module writes, and PNG for an extension it doesn't know. `quality` is for JPEG and lossy WebP. Throws a `video_error` when the file can't be written |
| `empty()` | `true` for a frame with nothing in it |
| `to_string()` | Such as `VideoFrame(64x48 yuv420p at 1.5 s)` |

The conversion to RGB uses the frame's own color matrix (BT.601, BT.709 or BT.2020) and range, and is done once per frame however many times `pixel` or `rgba` is called.

**The planes.** For your own processing (or to upload the planes to a GPU and convert them there), the frame's samples are available as they are:

| Method | Gives |
|---|---|
| `format()`, `format_name()` | The [`PixelFormat`](#enums), such as `PixelFormat::yuv420p`, and its name |
| `plane_count()` | 1 for gray frames, otherwise 3: Y (luma), then Cb and Cr (chroma) |
| `plane_width(index)`, `plane_height(index)` | A plane's size; for `yuv420p`, planes 1 and 2 are half the width and height |
| `bit_depth()`, `bytes_per_sample()`, `max_sample_value()` | Bits and bytes per sample, and the largest sample: 8, 1 and 255 for 8-bit video (the 10-, 12- and 14-bit formats take 2 bytes a sample, low byte first) |
| `chroma_shift_x()`, `chroma_shift_y()` | How many times the chroma planes are halved across and down: 1 and 1 for 4:2:0, 1 and 0 for 4:2:2, 0 and 1 for 4:4:0, 0 and 0 for 4:4:4 |
| `interlaced()`, `top_field_first()`, `structure_name()`, `repeat_fields()` | For interlaced video: whether the frame holds two fields, which comes first, how it was coded (`"frame"`, `"mbaff_frame"`, `"field_pair"`, `"top_field"` or `"bottom_field"`), and how many extra fields it is shown for |
| `plane(index)` | A plane's samples, row by row with no padding |
| `sample(index, x, y)` | One sample, as a number |
| `full_range()`, `color_matrix()` | Whether samples use 0 to 255 (otherwise 16 to 235), and `"bt601"`, `"bt709"`, `"bt2020"`, `"rgb"` or `"unspecified"` |
| `sar_num()`, `sar_den()`, `display_aspect()` | The pixels' shape (1:1 for square pixels) and the shape the frame should be shown at |

`plane` and `sample` throw a `video_error` for a plane or position that doesn't exist.

### Picture

A `Picture` is a frame already converted to RGBA: what [`Player::frame_rgba()`](#frame-loops) gives, and what `VideoFrame::to_picture()` makes. It has `width()`, `height()`, `pixels()` (the RGBA bytes, as `rgba()` above), `pixel(x, y)`, `save(path, quality = 90)`, `time()` (the frame's time), `serial()`, `empty()` and `to_string()`.

To draw a frame with [`graphics`](graphics.md) or edit it with [`images`](images.md), make an `Image` from its bytes: `std::images::Image::from_rgba(p.width(), p.height(), p.pixels())`.

```walnut
import module.video
import module.images

using std::images::Image;
using std::video::VideoFrame;

function main() -> int {
    VideoFrame: f = std::video::extract_frame("talk.mp4", 61.0);
    Image: shot = Image::from_rgba(f.width(), f.height(), f.rgba());
    shot.resized(320, 180).grayscale().save("talk-small.png");
    return 0;
} end function main
```

## Player

A `Player` plays a file: the sound through the speakers (or a [shared mixer](#sharing-a-mixer)), and the video as frames your program asks for and shows however it likes. Decoding happens on the player's own threads, a little ahead of time, so asking for a frame never waits for the decoder.

```walnut
import module.video
import module.io

using std::video::Player;

function main() -> int {
    Player: player;
    player.open("song.mp4");
    std::io::println("playing ", player.duration(), " s");
    player.play();
    player.wait();
    return 0;
} end function main
```

This plays a file to the end. Showing its pictures as well is in [frame loops](#frame-loops).

**Opening**

| Method | Does |
|---|---|
| `Player()` | A player with nothing open |
| `open(path)`, `open(bytes)` | Opens a file, or one in memory; throws a `video_error` when it can't. The player is then paused at the start. Opening another file closes the first |
| `open(path, options)`, `open(bytes, options)` | The same with [`PlayerOptions`](#playeroptions) |
| `set_options(options)` | Options for the next `open` |
| `close()`, `is_open()` | Closes the file and stops the player's threads. Going out of scope does the same |
| `info()` | The file's [`MediaInfo`](#probe-and-mediainfo) |
| `tracks()`, `duration()`, `has_video()`, `has_audio()`, `width()`, `height()`, `display_width()`, `display_height()`, `display_aspect()`, `rotation()`, `frame_rate()` | As in `MediaInfo`, for what's playing; `has_video()` is `false` when the video track can't be decoded |
| `video_track()`, `audio_track()` | The tracks in use (-1 for none) |
| `select_video_track(index)`, `select_audio_track(index)` | Switches to another track, such as another language; throws a `video_error` for a track that can't be played |
| `video_error()`, `audio_error()` | Why there is no picture or no sound, or `""` |

**Playing**

| Method | Does |
|---|---|
| `play()`, `pause()` | Starts and pauses. `play()` throws a `video_error` when nothing is open; `play()` after the end starts again from the beginning |
| `stop()` | Pauses and goes back to the start |
| `seek(seconds, accurate = true)` | Goes to a time. Accurate seeks land on that exact frame (decoding from the keyframe before it); `accurate = false` lands on the keyframe, which is quicker for scrubbing. Gives `false` if it couldn't. It happens in the background: `seeking()` is `true` until it's done |
| `position()` | Where it is, in seconds. While the sound plays, this is the sound's own clock, so pictures and sound stay together |
| `state()`, `state_name()` | A [`PlayerState`](#enums): `closed`, `paused`, `playing`, `ended` or `error` |
| `playing()`, `paused()`, `ended()`, `seeking()`, `buffering()` | The same as yes-or-no questions; `buffering()` is `true` while it's playing but waiting for the decoder |
| `error()` | What went wrong when the state is `error` |
| `wait(timeout_ms = -1)` | Waits until the end; `false` if it gave up first, or straight away when the player is paused, closed or has failed. A looping player never ends |
| `set_loop(on)`, `loop()` | Starts again at the end |
| `set_rate(rate)`, `rate()` | Speed: 2.0 plays twice as fast. Rates are kept between 0.25 and 4; a rate of 0 or less throws a `video_error` |
| `set_preserve_pitch(on)`, `preserve_pitch()` | Keeps voices sounding normal when the rate isn't 1 (on by default) |
| `set_volume(volume)`, `volume()`, `set_muted(on)`, `muted()` | The sound's volume (1.0 is as recorded) |
| `stats()` | A `PlayerStats`: `frames_decoded()`, `frames_presented()`, `frames_dropped()` (decoded too late to show), `frames_skipped()`, `packets_skipped()`, `decode_errors()`, `audio_underruns()`, `queued_packets()`, `queued_frames()`, `audio_buffered()` (seconds), `average_decode_ms()`, `average_convert_ms()`, `audio_clock()` (whether the sound is keeping time), `frames_deinterlaced()` and `deinterlacing()` (whether the last frame shown was deinterlaced) |

A `Player` can't be copied, but it can be moved.

### PlayerOptions

| Field | Default | Meaning |
|---|---|---|
| `video`, `audio` | `true`, `true` | Whether to decode the pictures and the sound. Without sound, a player keeps time with the system clock |
| `autoplay` | `false` | Starts playing as soon as it opens |
| `loop` | `false` | As `set_loop` |
| `rate`, `preserve_pitch` | `1.0`, `true` | As `set_rate` and `set_preserve_pitch` |
| `volume`, `muted` | `1.0`, `false` | As `set_volume` and `set_muted` |
| `rgba` | `false` | Converts each frame to RGBA on the player's thread, ahead of time, so `frame_rgba()` costs nothing more than a copy |
| `max_queued_frames` | `0` | How many decoded frames to keep ready (0 picks for itself) |
| `presentation_offset` | `0.0` | Seconds to show frames earlier (positive) or later than their time, to make up for a slow display |
| `audio_buffer_seconds` | `1.0` | How much sound to decode ahead |
| `sample_rate`, `channels` | `48000`, `2` | The format the player's own output plays at |
| `device`, `latency_ms` | `""`, `30` | The output device (`""` is the default; see [`audio::devices`](audio.md#devices)) and its latency |
| `deinterlace` | `Deinterlace::when_interlaced` | A [`Deinterlace`](#enums): `when_interlaced` deinterlaces frames the stream marks as interlaced, `on` deinterlaces every frame, `off` shows frames as decoded |
| `deinterlace_mode`, `deinterlace_field_rate` | `DeinterlaceMode::yadif`, `false` | How: `weave`, `bob`, `linear`, `yadif` or `yadif_spatial` (a [`DeinterlaceMode`](#enums)); with `deinterlace_field_rate`, each field becomes its own frame, doubling the frame rate |

`set_deinterlace(setting)`, `set_deinterlace_mode(mode, field_rate = false)`, `deinterlace()`, `deinterlace_mode()`, `deinterlace_name()` (`"off"`, `"auto"` or `"on"`), `deinterlace_mode_name()`, `deinterlace_field_rate()` and `deinterlacing()` change and read the same settings on a player that's already open.

With no working sound device, the player plays without sound and sends a `warning` event; with the `"null"` audio backend (`FIZMO_AUDIO_BACKEND=null`), it plays in silence on a timer, which is useful for tests.

## Frame loops

A player doesn't draw anything itself. Your program asks it for the current frame, as often as it likes (usually once per screen refresh or per loop), and draws it:

| Method | Gives |
|---|---|
| `frame()` | The frame that should be showing now, as an `Optional<VideoFrame>`, but only when it's a different frame from the last `frame()` call; otherwise it's empty, so there's nothing new to draw |
| `frame_rgba()` | The same as a `Picture`, already in RGBA; it keeps its own count, so it can be used beside `frame()` |
| `current_frame()` | The frame showing now, even if it hasn't changed (to redraw a window) |
| `frame_serial()` | The number of the frame showing now. It goes up by one for each new frame, so a program can tell a new frame by comparing it with `VideoFrame::serial()` |
| `frame_time()` | The time of the frame showing now, or -1 when there is none |
| `next_frame_delay()` | Seconds until the next frame is due, 0 when one is already due, or -1 when the player doesn't know (paused, ended, no video or still decoding) |
| `next_frame_delay_ms(fallback_ms = 10)` | The same in whole milliseconds, ready for `sleep_for`, with `fallback_ms` in place of -1 |

While paused, the frame at the current position is shown (so after a seek, `frame()` gives the new frame once). Late frames are dropped rather than shown out of time; `stats().frames_dropped()` counts them.

Here is a complete program that plays a file's sound while saving each new frame's average brightness, sleeping just long enough between frames:

```walnut
import module.video
import module.io
import module.threading

using std::optional::Optional;
using std::video::Player;
using std::video::VideoFrame;

function brightness(const VideoFrame&: f) -> long int {
    std::collections::Vector<std::hardware_uint8>: luma = f.plane(0);
    long int: total = 0;
    for (std::hardware_uint8: y in luma) {
        total += static_cast<long int>(y);
    }
    return total / luma.size();
} end function brightness

function main() -> int {
    Player: player;
    player.open("clip.mp4");
    player.play();
    while (player.playing()) {
        if (Optional<VideoFrame>: f = player.frame()) then {
            std::io::println(f.value().pts(), " s: brightness ", brightness(f.value()));
        }
        std::threading::this_thread::sleep_for(player.next_frame_delay_ms());
    }
    return 0;
} end function main
```

To show the frames, draw them in your window's loop instead. With [`graphics`](graphics.md), turn each new `Picture` into an `Image` and draw that until the next one arrives; the window's refresh paces the loop:

```walnut
import module.video
import module.graphics

using std::graphics::Renderer;
using std::images::Image;
using std::optional::Optional;
using std::video::Picture;
using std::video::Player;

function main() -> int {
    Player: player;
    player.open("clip.mp4");
    std::windows::Window: win = std::windows::Window::open("Video", player.width(), player.height());
    Renderer: r = Renderer::make(win);
    Image: shown;
    player.play();
    while (win.poll() && !player.ended()) {
        if (Optional<Picture>: p = player.frame_rgba()) then {
            shown = Image::from_rgba(p.value().width(), p.value().height(), p.value().pixels());
        }
        r.begin_frame();
        r.clear(std::colors::black);
        if (!shown.is_empty()) then {
            r.draw_image(shown, 0.0, 0.0);
        }
        r.present();
    }
    return 0;
} end function main
```

**Threads.** Call a player's methods from one thread (usually the main one). The frames and pictures it gives are values that any thread can then use.

## Events

A player reports what happens as `PlayerEvent`s. There are two ways to get them.

**Polling.** `events()` gives the events since the last call, oldest first, as a `Vector<PlayerEvent>` (a player keeps up to 512):

```walnut
for (const std::video::PlayerEvent&: e in player.events()) {
    if (e.type() == std::video::EventType::ended) then {
        std::io::println("done");
    } else if (e.type() == std::video::EventType::warning) then {
        std::io::println("warning: ", e.message());
    }
}
```

**A callback.** `on_event(callback)` calls a `function(const PlayerEvent&) -> void` for every event as it happens; `clear_on_event()` stops it. Polling keeps working beside it.

```walnut
player.on_event([&](const std::video::PlayerEvent&: e) -> void {
    if (e.type() == std::video::EventType::looped) then {
        laps.fetch_add(1);
    }
});
```

A `PlayerEvent` has `type()` (an [`EventType`](#enums)), `name()` (such as `"seeked"`), `position()` (where the player was) and `message()` (for `warning` and `error`), and `to_string()`.

| Event | When |
|---|---|
| `loaded` | The file opened |
| `first_frame` | The first frame after opening is ready |
| `play`, `pause` | `play()` or `pause()` was called |
| `playing`, `waiting` | Playing went on again after waiting for the decoder, or had to wait |
| `seeking`, `seeked` | A seek started and finished |
| `ended`, `looped` | The end was reached, or it started again because of `loop` |
| `rate_changed`, `volume_changed`, `track_changed` | A setting changed; for `track_changed`, `message()` is `"video"` or `"audio"` |
| `warning` | Something didn't work but playing goes on, such as a video codec that isn't supported or no sound device; `message()` says what |
| `error` | Playing stopped; `message()` and `error()` say why |

**The callback runs on the player's thread.** It's called on the player's own worker thread while the rest of the program goes on, as [audio streams](audio.md#the-audio-thread) are. So:
- **Keep it short.** The player can't read the file while the callback runs. Set a flag or count something, and do the real work in your own loop.
- **Share data safely.** Use `std::threading::Atomic` or a `Mutex` for anything both sides change.
- **Don't call the player from the callback**, except for reading its state: control it from your own thread.
- **Keep what it uses alive.** `close()`, `clear_on_event()` and the player going out of scope wait for a callback that's running to finish, and none runs after them, so declare the player after the variables a `[&]` lambda uses.
- **Exceptions stop it.** An exception the callback doesn't catch stops further calls; `callback_error()` gives its message.
- **`--gc` is safe.** While a callback is set, [garbage collection](../../documentation.md#safety-checks) waits, so it never frees something the callback is using; it catches up after `clear_on_event()` or `close()`.

## Sharing a Mixer

By default, each player opens its own sound output. `attach(mixer)` plays the sound through an [`audio::Mixer`](audio.md#playing-sounds-mixer) instead, so a game or app can play several videos, sound effects and music through one output, with the mixer's effects and master volume. The video then keeps time with that mixer.

```walnut
import module.video
import module.audio

function main() -> int {
    std::audio::Mixer: mixer;
    mixer.start();
    std::video::Player: player;
    player.attach(mixer);
    player.open("intro.mp4");
    player.play();
    mixer.play(std::audio::AudioBuffer::load("click.wav"));
    player.wait();
    return 0;
} end function main
```

`detach()` goes back to the player's own output, and `attached()` says whether it's sharing one. The player keeps the mixer alive while attached. Start the mixer with `start()` (or use its `render`) before playing; a mixer that isn't running doesn't move the clock forward.

## Demuxer: packets

A `Demuxer` reads a container's tracks and their compressed packets, without decoding them: for copying streams, counting frames, or feeding your own decoder.

```walnut
import module.video
import module.io

using std::optional::Optional;
using std::video::Demuxer;
using std::video::Packet;

function main() -> int {
    Demuxer: file = Demuxer::open("clip.mp4");
    long int: keyframes = 0;
    while (true) {
        Optional<Packet>: p = file.read();
        if (!p.has_value()) then {
            break;
        }
        if (p.value().track() == 0 && p.value().keyframe()) then {
            keyframes += 1;
        }
    }
    std::io::println(keyframes, " keyframes");
    return 0;
} end function main
```

| Method | Does |
|---|---|
| `Demuxer::open(path)`, `Demuxer::open(bytes)` | Opens a file; throws a `video_error` when it can't |
| `tracks()`, `track(index)`, `track_count()` | The tracks, as for `MediaInfo` |
| `duration()`, `format_name()`, `tag(key)`, `tag_keys()` | About the file |
| `best_track(kind)` | The index of the main track of a [`MediaType`](#enums), or -1 |
| `select(index, on = true)`, `select_only(index)`, `selected(index)` | Which tracks' packets `read` gives (all of them at first) |
| `read()` | The next packet in file order, as an `Optional<Packet>`; empty at the end |
| `seek(seconds, keyframe_before = true)` | Moves so the next packets start at that time; with `keyframe_before`, at the keyframe at or before it, where a decoder can start |
| `rewind()` | Back to the start |
| `close()`, `is_open()` | |

A `Packet` has `track()` (the track's index), `pts()` and `dts()` (when it's shown and when it's decoded, in seconds), `duration()`, `keyframe()`, `size()`, `data()` (its bytes), `position()` (where it is in the file) and `discard_padding()`. A `Demuxer` can be moved but not copied.

## VideoDecoder and H264Decoder

`VideoDecoder::for_track(track)` makes a decoder for a demuxer's video track, and throws a `video_error` that names the codec when it isn't supported. Packets go in with `decode(packet)` (or `decode(bytes, pts)`), and frames come out with `receive()`, in display order; since H.264 can reorder frames, a frame can come out a few packets after its own. At the end, `flush()` lets out the frames still inside.

```walnut
Demuxer: file = Demuxer::open("clip.mp4");
long int: v = file.best_track(MediaType::video);
file.select_only(v);
VideoDecoder: decoder = VideoDecoder::for_track(file.track(v));
while (true) {
    Optional<Packet>: p = file.read();
    if (!p.has_value()) then {
        break;
    }
    decoder.decode(p.value());
    while (true) {
        Optional<VideoFrame>: f = decoder.receive();
        if (!f.has_value()) then {
            break;
        }
        f.value().save(static_cast<string>(`frame-${decoder.frames_decoded()}.png`));
    }
}
```

`decode` throws a `video_error` for a packet it can't decode, such as a damaged one, and `error_count()` counts them; catch it to skip the packet and go on. A decoder also has `reset()` (to start again after a seek), `codec()`, `width()`, `height()` and `frames_decoded()`.

**H264Decoder** is the H.264 decoder by itself, for streams that don't come from a container (a camera, a network stream or a `.h264` file):

| Method | Does |
|---|---|
| `H264Decoder()` | A decoder that isn't open yet |
| `open(avcc)` | Opens it with an `avcC` record, from `Track::codec_config()`, for length-prefixed packets as MP4 and Matroska store them; throws a `video_error` for a record it can't read |
| `open_annexb()` | Opens it for Annex B data: NAL units after `00 00 01` start codes, as `.h264` files and most streams use |
| `decode(bytes, pts = 0.0)`, `receive()`, `flush()`, `reset()`, `close()`, `is_open()` | As above |
| `profile()`, `level()`, `chroma_format()`, `bit_depth()`, `width()`, `height()`, `coded_width()`, `coded_height()`, `progressive()`, `cabac()`, `frame_rate()` | What the stream's parameter sets say; they're known once the first picture has been decoded |
| `frames_decoded()`, `error_count()` | Counts |

`H264Reader::open(path)` (or `open(bytes)`) splits an Annex B file into access units (one picture each): `read()` gives the next as an `Optional<Vector<std::hardware_uint8>>`, ready for `decode`, and `rewind()` starts again. `std::video::is_annexb(bytes)` tells whether data starts with a start code, and `std::video::container_of(bytes)` names a file's container from its first bytes (`"mp4"`, `"webm"`, `"matroska"`, `"ogg"`, `"ivf"`, `"av1"`, `"hevc"` or `""`).

## Sound from video files

The [`audio`](audio.md) module reads the sound of video files too: `std::audio::load("film.mp4")` decodes an MP4, M4A, MOV, MKV, WebM or Ogg file's main audio track into an `AudioBuffer`, and `Mixer::play_file` streams it. So for sound alone, no `Player` is needed:

```walnut
std::audio::AudioBuffer: soundtrack = std::audio::load("film.mp4");
soundtrack.save("soundtrack.flac");
```

## Enums

| Enum | Values |
|---|---|
| `MediaType` | `video`, `audio`, `subtitle`, `other` |
| `PixelFormat` | `none`, `gray`, `yuv420p`, `yuv422p`, `yuv444p`, `gray10`, `yuv420p10`, `yuv422p10`, `yuv444p10`, `gray12`, `yuv420p12`, `yuv422p12`, `yuv444p12`, `gray14`, `yuv420p14`, `yuv422p14`, `yuv444p14`, `yuv440p`, `yuv440p10`, `yuv440p12` |
| `Deinterlace` | `off`, `when_interlaced`, `on` |
| `DeinterlaceMode` | `weave`, `bob`, `linear`, `yadif`, `yadif_spatial` |
| `PlayerState` | `closed`, `paused`, `playing`, `ended`, `error` |
| `EventType` | `loaded`, `first_frame`, `play`, `pause`, `playing`, `waiting`, `seeking`, `seeked`, `ended`, `looped`, `rate_changed`, `volume_changed`, `track_changed`, `warning`, `error` |

## Errors

Files that can't be read, codecs that aren't supported, positions outside a frame and the other mistakes above throw a `std::errors::video_error`. Its `what()` (or its `message` field) says what went wrong:

```walnut
import module.video
import module.io

function main() -> int {
    try {
        std::video::extract_frame("clip.ogv", 1.0);
    } catch (const std::errors::video_error&: e) {
        std::io::println("problem: ", e.what());
    }
    return 0;
} end function main
```

For a Theora file, this prints `problem: can't get a frame from 'clip.ogv' at 1 s: ` followed by a message that names Theora and lists the video codecs that decode (H.264, H.265, AV1 and VP9).
