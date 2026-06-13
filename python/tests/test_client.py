import unittest
import json
from unittest.mock import MagicMock, patch

from lazerdeck import Engine, Deck, DeckState, ValidationError, APIError, ConnectionError


class TestLazerdeckClient(unittest.TestCase):

    def test_deck_basic_actions(self):
        engine = Engine(url="http://localhost:8203")
        engine._post = MagicMock(return_value={"ok": True})

        # Test load
        engine.d1.load("C:/music/track.wav")
        engine._post.assert_called_with({
            "actions": [{"action": "load", "deck": "d1", "path": "C:/music/track.wav"}]
        })

        # Test play
        engine.d1.play()
        engine._post.assert_called_with({
            "actions": [{"action": "play", "deck": "d1"}]
        })

        # Test pause with on_beat
        engine.d2.pause(on_beat=32.5)
        engine._post.assert_called_with({
            "actions": [{"action": "pause", "deck": "d2", "onBeat": 32.5}]
        })

        # Test seek
        engine.d1.seek(-10)
        engine._post.assert_called_with({
            "actions": [{"action": "seek", "deck": "d1", "value": -10.0}]
        })

        # Test eq_low
        engine.d1.eq_low(0.2)
        engine._post.assert_called_with({
            "actions": [{"action": "eq_low", "deck": "d1", "value": 0.2}]
        })

        # Test metronome
        engine.d1.metronome(on=False)
        engine._post.assert_called_with({
            "actions": [{"action": "metronome", "deck": "d1", "on": False}]
        })

    def test_client_validation(self):
        engine = Engine()

        # Path validation
        with self.assertRaises(ValidationError):
            engine.d1.load("")

        with self.assertRaises(ValidationError):
            engine.d1.load(123)  # type: ignore

        # EQ validation
        with self.assertRaises(ValidationError):
            engine.d1.eq_low(1.1)

        with self.assertRaises(ValidationError):
            engine.d1.eq_mid(-0.1)

        # Volume validation
        with self.assertRaises(ValidationError):
            engine.d1.volume(2.0)

        # Speed validation
        with self.assertRaises(ValidationError):
            engine.d1.speed(-0.5)

        with self.assertRaises(ValidationError):
            engine.d1.speed(0.0)

        # on_beat validation
        with self.assertRaises(ValidationError):
            engine.d1.play(on_beat=0.5)

    def test_batching(self):
        engine = Engine()
        engine._post = MagicMock(return_value={"ok": True})

        with engine.batch():
            engine.d1.load("C:/track1.mp3")
            engine.d1.play()
            engine.d2.volume(0.0)

        engine._post.assert_called_once_with({
            "actions": [
                {"action": "load", "deck": "d1", "path": "C:/track1.mp3"},
                {"action": "play", "deck": "d1"},
                {"action": "volume", "deck": "d2", "value": 0.0}
            ]
        })

    def test_align(self):
        engine = Engine()
        engine._post = MagicMock(return_value={"ok": True})

        # Call directly on Engine
        engine.align(engine.d1, 32.0, engine.d2, 64.0)
        engine._post.assert_called_with({
            "actions": [{
                "action": "align",
                "deck": "d1",
                "subjectBeat": 32.0,
                "reference": "d2",
                "referenceBeat": 64.0
            }]
        })

        # Call on Deck proxy
        engine.d2.align(16.0, engine.d1, 32.0)
        engine._post.assert_called_with({
            "actions": [{
                "action": "align",
                "deck": "d2",
                "subjectBeat": 16.0,
                "reference": "d1",
                "referenceBeat": 32.0
            }]
        })

    @patch("urllib.request.urlopen")
    def test_get_state(self, mock_urlopen):
        # Setup mock response
        mock_response = MagicMock()
        mock_response.read.return_value = json.dumps({
            "ok": True,
            "decks": [
                {
                    "deck": "d1",
                    "bpm": 128.0,
                    "beatOffset": 1000.0,
                    "speed": 1.0,
                    "isPlaying": True,
                    "isLoading": False,
                    "isAnalyzing": False,
                    "loopActive": True,
                    "currentFrame": 441000,
                    "loopStart": 0,
                    "loopEnd": 882000,
                    "recallStart": 0,
                    "recallEnd": 0,
                    "syncActive": True,
                    "syncSource": 1,
                    "sampleRate": 44100,
                    "metronomeEnabled": False,
                    "eqLow": 0.5,
                    "eqMid": 0.5,
                    "eqHigh": 0.5,
                    "volume": 0.8,
                    "filepath": "C:/music/a.wav"
                }
            ]
        }).encode("utf-8")
        mock_urlopen.return_value.__enter__.return_value = mock_response

        engine = Engine()
        states = engine.get_state()

        self.assertEqual(len(states), 1)
        state = states[0]
        self.assertEqual(state.deck, "d1")
        self.assertEqual(state.bpm, 128.0)
        self.assertTrue(state.is_playing)
        self.assertTrue(state.loop_active)
        self.assertEqual(state.current_time, 10.0)  # 441000 / 44100
        self.assertEqual(state.volume, 0.8)
        self.assertEqual(state.filepath, "C:/music/a.wav")


if __name__ == "__main__":
    unittest.main()
