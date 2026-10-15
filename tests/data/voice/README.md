# Voice director test clips

Synthesized command utterances for `tests/test_whisper.cpp` (embedded
recognizer end-to-end test). 16-bit PCM mono WAV at the rate noted; the test
mixes in -70 dBFS hiss and runs them through the same resampler, utterance
segmenter, whisper engine and intent parser the plugin uses.

| File | Rate | Text | Expected |
|------|------|------|----------|
| `ja_bgm_down.wav` | 48 kHz | 音響さん、BGM下げて。 | BGM trim down |
| `ja_game_up.wav` | 24 kHz | 音響さん、ゲーム音上げて。 | game trim up |
| `ja_mute.wav` | 48 kHz | 音響さん、マイクミュート。 | mute mic |
| `ja_chapter.wav` | 24 kHz | ディレクター、チャプター。 | chapter mark |
| `ja_undo.wav` | 24 kHz | 音響さん、元に戻して。 | undo |
| `ja_neg.wav` | 24 kHz | 今日はいい天気ですね。 | must not fire |
| `en_music_down.wav` | 22.05 kHz | OK director, turn down the music. | BGM trim down |
| `en_mute.wav` | 22.05 kHz | Hey director, mute. | mute mic |
| `en_neg.wav` | 22.05 kHz | That was a really good game today. | must not fire |

Pass criteria: no wrong or spurious command fires, at least 70 % of the
commands recognized, and the threaded bridge fires both commands of a two-clip
48 kHz stream.

## Provenance

- Japanese: Open JTalk 1.11 (BSD-3-Clause) with the HTS voice
  "nitech_jp_atr503_m001" — Copyright 2003-2012 Nagoya Institute of
  Technology, licensed under CC BY 3.0
  (https://creativecommons.org/licenses/by/3.0/). The clips are derived
  output of that voice.
- English: eSpeak NG (`espeak-ng -v en-us`). eSpeak NG is GPL-3.0; its speech
  output is not covered by the program's licence.

Regenerate (Ubuntu: `open-jtalk open-jtalk-mecab-naist-jdic
hts-voice-nitech-jp-atr503-m001 espeak-ng ffmpeg`):

```bash
echo "音響さん、BGM下げて。" | open_jtalk -x /var/lib/mecab/dic/open-jtalk/naist-jdic \
  -m /usr/share/hts-voice/nitech-jp-atr503-m001/nitech_jp_atr503_m001.htsvoice -ow ja_bgm_down.wav
espeak-ng -v en-us -w en_mute.wav "Hey director, mute."
ffmpeg -i in.wav -ar 24000 -ac 1 -c:a pcm_s16le out.wav   # the 24 kHz clips
```
