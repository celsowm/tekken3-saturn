import sys
import unittest
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
import encode_ima_stream as ima


def tone(frames, freq=1000.0, rate=44100):
    t = np.arange(frames) / rate
    x = 8000 * np.sin(2 * np.pi * freq * t)
    return np.stack([x, 0.5 * x], axis=1).round().astype(np.int64)


class EncodeImaStreamTest(unittest.TestCase):
    def test_block_size_and_padding(self):
        frames = 3000                                    # three blocks, the last one padded
        data, _ = ima.encode(tone(frames))
        self.assertEqual(len(data), 3 * ima.BLOCK_BYTES)
        self.assertEqual(len(data) % 4, 0)

    def test_decoder_mirror_matches_encoder_reconstruction(self):
        rng = np.random.default_rng(7)
        samples = np.clip(rng.normal(0, 4000, (5000, 2)), -32768, 32767).round().astype(np.int64)
        data, recon = ima.encode(samples)
        self.assertTrue(np.array_equal(ima.decode_blocks(data, len(samples)), recon))

    def test_block_header_holds_the_first_sample_and_a_valid_index(self):
        samples = tone(4096)
        data, _ = ima.encode(samples)
        blocks = np.frombuffer(data, dtype=np.uint8).reshape(-1, ima.BLOCK_BYTES)
        for b in range(blocks.shape[0]):
            for channel in range(2):
                head = blocks[b, 4 * channel:4 * channel + 4]
                predictor = (int(head[0]) << 8) | int(head[1])
                predictor = predictor - 65536 if predictor >= 32768 else predictor
                self.assertEqual(predictor, samples[b * ima.BLOCK_FRAMES, channel])
                self.assertLessEqual(int(head[2]), 88)

    def test_codec_error_on_a_tone_is_small(self):
        samples = tone(20000)
        _, recon = ima.encode(samples)
        self.assertGreater(ima.snr_db(samples.astype(np.float64), recon[:len(samples)].astype(np.float64)), 25.0)


if __name__ == '__main__':
    unittest.main()
