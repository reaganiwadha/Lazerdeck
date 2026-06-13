import json
import threading
import urllib.request
import urllib.error
from typing import List, Dict, Any, Optional, Union, Generator
from contextlib import contextmanager

from .exceptions import ValidationError, ConnectionError, APIError

DEFAULT_URL = "http://127.0.0.1:8203"


class DeckState:
    """Representation of the state of a Lazerdeck deck."""

    def __init__(self, data: Dict[str, Any]) -> None:
        self.deck: str = data.get("deck", "")
        self.bpm: float = float(data.get("bpm", 0.0))
        self.beat_offset: float = float(data.get("beatOffset", 0.0))
        self.speed: float = float(data.get("speed", 1.0))
        self.is_playing: bool = bool(data.get("isPlaying", False))
        self.is_loading: bool = bool(data.get("isLoading", False))
        self.is_analyzing: bool = bool(data.get("isAnalyzing", False))
        self.loop_active: bool = bool(data.get("loopActive", False))
        self.current_frame: int = int(data.get("currentFrame", 0))
        self.loop_start: int = int(data.get("loopStart", 0))
        self.loop_end: int = int(data.get("loopEnd", 0))
        self.recall_start: int = int(data.get("recallStart", 0))
        self.recall_end: int = int(data.get("recallEnd", 0))
        self.sync_active: bool = bool(data.get("syncActive", False))
        self.sync_source: int = int(data.get("syncSource", -1))
        self.sample_rate: int = int(data.get("sampleRate", 44100))
        self.metronome_enabled: bool = bool(data.get("metronomeEnabled", False))
        self.eq_low: float = float(data.get("eqLow", 0.5))
        self.eq_mid: float = float(data.get("eqMid", 0.5))
        self.eq_high: float = float(data.get("eqHigh", 0.5))
        self.volume: float = float(data.get("volume", 1.0))
        self.filepath: str = data.get("filepath", "")

    @property
    def current_time(self) -> float:
        """Current playback position in seconds."""
        if self.sample_rate > 0:
            return self.current_frame / self.sample_rate
        return 0.0

    def __repr__(self) -> str:
        return (
            f"<DeckState {self.deck} playing={self.is_playing} "
            f"bpm={self.bpm:.2f} time={self.current_time:.2f}s file={self.filepath}>"
        )


class Deck:
    """Proxy for a specific deck in the Lazerdeck engine."""

    def __init__(self, engine: "Engine", name: str) -> None:
        self._engine = engine
        self.name = name  # "d1", "d2", etc.

    def _validate_on_beat(self, on_beat: Optional[float]) -> None:
        if on_beat is not None:
            if not isinstance(on_beat, (int, float)):
                raise ValidationError("on_beat must be a number")
            if on_beat < 1.0:
                raise ValidationError("on_beat must be >= 1.0")

    def _validate_range(self, name: str, value: float, min_val: float = 0.0, max_val: float = 1.0) -> None:
        if not isinstance(value, (int, float)):
            raise ValidationError(f"{name} must be a number")
        if not (min_val <= value <= max_val):
            raise ValidationError(f"{name} must be in range [{min_val}, {max_val}]")

    def _act(self, action: str, on_beat: Optional[float] = None, **kwargs) -> Optional[Dict[str, Any]]:
        self._validate_on_beat(on_beat)
        payload = {"action": action, "deck": self.name}
        if on_beat is not None:
            payload["onBeat"] = on_beat
        for k, v in kwargs.items():
            if v is not None:
                payload[k] = v
        return self._engine._dispatch(payload)

    def load(self, path: str, on_beat: Optional[float] = None) -> Optional[Dict[str, Any]]:
        """Decode and analyze an audio file onto the deck.

        BPM and beatgrid are auto-detected. Replaces any current track.
        """
        if not path or not isinstance(path, str):
            raise ValidationError("path must be a non-empty string")
        # Forward slashes work on Windows and are standard
        normalized_path = path.replace("\\", "/")
        return self._act("load", on_beat=on_beat, path=normalized_path)

    def eject(self, on_beat: Optional[float] = None) -> Optional[Dict[str, Any]]:
        """Unload the current track.

        Resets tempo, loops, sync, speed, and drops automation/scheduled triggers.
        """
        return self._act("eject", on_beat=on_beat)

    def play(self, on_beat: Optional[float] = None) -> Optional[Dict[str, Any]]:
        """Start playback."""
        return self._act("play", on_beat=on_beat)

    def pause(self, on_beat: Optional[float] = None) -> Optional[Dict[str, Any]]:
        """Pause playback, keeping the current position."""
        return self._act("pause", on_beat=on_beat)

    def stop(self, on_beat: Optional[float] = None) -> Optional[Dict[str, Any]]:
        """Pause and seek back to the start."""
        return self._act("stop", on_beat=on_beat)

    def seek(self, seconds: float, on_beat: Optional[float] = None) -> Optional[Dict[str, Any]]:
        """Seek by a relative offset in seconds (can be negative)."""
        if not isinstance(seconds, (int, float)):
            raise ValidationError("seconds must be a number")
        return self._act("seek", on_beat=on_beat, value=float(seconds))

    def speed(self, multiplier: float, on_beat: Optional[float] = None) -> Optional[Dict[str, Any]]:
        """Set the playback speed multiplier.

        1.0 = normal, 2.0 = double speed. Must be greater than 0.
        """
        if not isinstance(multiplier, (int, float)):
            raise ValidationError("multiplier must be a number")
        if multiplier <= 0:
            raise ValidationError("multiplier must be greater than 0")
        return self._act("speed", on_beat=on_beat, speed=float(multiplier))

    def speed_reset(self, on_beat: Optional[float] = None) -> Optional[Dict[str, Any]]:
        """Reset the playback speed multiplier to 1.0."""
        return self._act("speed_reset", on_beat=on_beat)

    def playjump(self, beat: float, on_beat: Optional[float] = None) -> Optional[Dict[str, Any]]:
        """Jump to the specified 1-based beat and start playback."""
        if not isinstance(beat, (int, float)):
            raise ValidationError("beat must be a number")
        if beat < 1.0:
            raise ValidationError("beat must be >= 1.0")
        return self._act("playjump", on_beat=on_beat, beat=float(beat))

    def bpm(self, bpm_value: float, on_beat: Optional[float] = None) -> Optional[Dict[str, Any]]:
        """Manually set and pin the deck BPM (persisted to the analysis database)."""
        if not isinstance(bpm_value, (int, float)):
            raise ValidationError("BPM must be a number")
        if bpm_value <= 0:
            raise ValidationError("BPM must be greater than 0")
        return self._act("bpm", on_beat=on_beat, value=float(bpm_value))

    def offset(self, frames: float, on_beat: Optional[float] = None) -> Optional[Dict[str, Any]]:
        """Set the beat-grid origin offset in sample frames."""
        if not isinstance(frames, (int, float)):
            raise ValidationError("offset frames must be a number")
        return self._act("offset", on_beat=on_beat, value=float(frames))

    def nudge_offset(self, frames_delta: float, on_beat: Optional[float] = None) -> Optional[Dict[str, Any]]:
        """Nudge the beat-grid origin by a delta in sample frames."""
        if not isinstance(frames_delta, (int, float)):
            raise ValidationError("nudge frames must be a number")
        return self._act("nudge_offset", on_beat=on_beat, value=float(frames_delta))

    def reanalyze(self, on_beat: Optional[float] = None) -> Optional[Dict[str, Any]]:
        """Drop the cached analysis and re-run BPM detection."""
        return self._act("reanalyze", on_beat=on_beat)

    def metronome(self, on: bool = True, on_beat: Optional[float] = None) -> Optional[Dict[str, Any]]:
        """Toggle the grid metronome click."""
        if not isinstance(on, bool):
            raise ValidationError("on must be a boolean")
        return self._act("metronome", on_beat=on_beat, on=on)

    def sync(self, on: bool = True, on_beat: Optional[float] = None) -> Optional[Dict[str, Any]]:
        """Set the deck's sync intent.

        Followers phase-lock and track the elected master deck's tempo.
        """
        if not isinstance(on, bool):
            raise ValidationError("on must be a boolean")
        return self._act("sync", on_beat=on_beat, on=on)

    def master(self, on_beat: Optional[float] = None) -> Optional[Dict[str, Any]]:
        """Force this deck to be the sync master (holds as long as it plays)."""
        return self._act("master", on_beat=on_beat)

    def eq_low(self, value: float, on_beat: Optional[float] = None) -> Optional[Dict[str, Any]]:
        """Set the low EQ level (0.5 = unity, range [0.0, 1.0])."""
        self._validate_range("eq_low value", value)
        return self._act("eq_low", on_beat=on_beat, value=float(value))

    def eq_mid(self, value: float, on_beat: Optional[float] = None) -> Optional[Dict[str, Any]]:
        """Set the mid EQ level (0.5 = unity, range [0.0, 1.0])."""
        self._validate_range("eq_mid value", value)
        return self._act("eq_mid", on_beat=on_beat, value=float(value))

    def eq_high(self, value: float, on_beat: Optional[float] = None) -> Optional[Dict[str, Any]]:
        """Set the high EQ level (0.5 = unity, range [0.0, 1.0])."""
        self._validate_range("eq_high value", value)
        return self._act("eq_high", on_beat=on_beat, value=float(value))

    def volume(self, value: float, on_beat: Optional[float] = None) -> Optional[Dict[str, Any]]:
        """Set the channel volume (0.5 = unity, range [0.0, 1.0])."""
        self._validate_range("volume value", value)
        return self._act("volume", on_beat=on_beat, value=float(value))

    def align(self, subject_beat: float, reference: Union["Deck", str], reference_beat: float) -> Optional[Dict[str, Any]]:
        """Align this deck (as the subject) so its subject_beat aligns with the reference deck's reference_beat."""
        ref_deck = reference if isinstance(reference, str) else reference.name
        return self._engine.align(self, subject_beat, ref_deck, reference_beat)

    def __repr__(self) -> str:
        return f"<Deck {self.name}>"


class Engine:
    """Client for interacting with the Lazerdeck engine."""

    d1: Deck
    d2: Deck
    d3: Deck
    d4: Deck
    d5: Deck
    d6: Deck
    d7: Deck
    d8: Deck
    d9: Deck
    d10: Deck

    def __init__(self, url: str = DEFAULT_URL, deck_count: int = 2, timeout: float = 3.0) -> None:
        self.url = url.rstrip("/")
        self.deck_count = deck_count
        self.timeout = timeout
        self._local = threading.local()

        for i in range(1, deck_count + 1):
            setattr(self, f"d{i}", Deck(self, f"d{i}"))

    def __getitem__(self, item: int) -> Deck:
        """Get a deck proxy by 1-based index (e.g., engine[1])."""
        if not isinstance(item, int):
            raise TypeError("Deck index must be an integer")
        if not (1 <= item <= self.deck_count):
            raise IndexError(f"Deck index {item} out of range (1..{self.deck_count})")
        return getattr(self, f"d{item}")

    def health(self) -> bool:
        """Check if the engine control server is running and responding."""
        try:
            url = f"{self.url}/health"
            req = urllib.request.Request(url, method="GET")
            with urllib.request.urlopen(req, timeout=self.timeout) as r:
                data = json.loads(r.read().decode("utf-8"))
                return bool(data.get("ok", False))
        except (urllib.error.URLError, json.JSONDecodeError):
            return False

    def get_state(self) -> List[DeckState]:
        """Fetch the current state of all decks from the engine."""
        try:
            url = f"{self.url}/state"
            req = urllib.request.Request(url, method="GET")
            with urllib.request.urlopen(req, timeout=self.timeout) as r:
                data = json.loads(r.read().decode("utf-8"))
                if not data.get("ok", False):
                    raise APIError(data.get("error", "Unknown error fetching state"))
                return [DeckState(deck_data) for deck_data in data.get("decks", [])]
        except urllib.error.HTTPError as e:
            try:
                err_data = json.loads(e.read().decode("utf-8"))
                raise APIError(err_data.get("error", str(e)))
            except Exception:
                raise APIError(f"HTTP error {e.code}: {e.reason}")
        except urllib.error.URLError as e:
            raise ConnectionError(f"Failed to connect to Lazerdeck engine: {e.reason}")

    def align(
        self,
        subject: Union[Deck, str],
        subject_beat: float,
        reference: Union[Deck, str],
        reference_beat: float
    ) -> Optional[Dict[str, Any]]:
        """Phase-align the subject deck's beat with the reference deck's beat.

        Example:
            engine.align(engine.d1, 32.0, engine.d2, 64.0)
        """
        subj_name = subject if isinstance(subject, str) else subject.name
        ref_name = reference if isinstance(reference, str) else reference.name

        if not subj_name.startswith("d") or not ref_name.startswith("d"):
            raise ValidationError("Invalid deck names for align")
        if not isinstance(subject_beat, (int, float)) or subject_beat < 1.0:
            raise ValidationError("subject_beat must be a number >= 1.0")
        if not isinstance(reference_beat, (int, float)) or reference_beat < 1.0:
            raise ValidationError("reference_beat must be a number >= 1.0")

        return self._dispatch({
            "action": "align",
            "deck": subj_name,
            "subjectBeat": float(subject_beat),
            "reference": ref_name,
            "referenceBeat": float(reference_beat),
        })

    @contextmanager
    def batch(self) -> Generator["BatchContext", None, None]:
        """Context manager to queue multiple actions into a single atomic POST request.

        Example:
            with engine.batch():
                engine.d1.load("C:/music/track.mp3")
                engine.d1.play()
        """
        if getattr(self._local, "batch_queue", None) is not None:
            # Already in a batch, yield context directly (nesting support)
            yield BatchContext(self)
            return

        self._local.batch_queue = []
        try:
            yield BatchContext(self)
            actions = self._local.batch_queue
        finally:
            self._local.batch_queue = None

        if actions:
            self._post({"actions": actions})

    def _dispatch(self, action: Dict[str, Any]) -> Optional[Dict[str, Any]]:
        queue = getattr(self._local, "batch_queue", None)
        if queue is not None:
            queue.append(action)
            return None
        return self._post({"actions": [action]})

    def _post(self, payload: Dict[str, Any]) -> Dict[str, Any]:
        url = f"{self.url}/action"
        data = json.dumps(payload).encode("utf-8")
        req = urllib.request.Request(
            url,
            data=data,
            headers={"Content-Type": "application/json"},
            method="POST"
        )
        try:
            with urllib.request.urlopen(req, timeout=self.timeout) as r:
                return json.loads(r.read().decode("utf-8"))
        except urllib.error.HTTPError as e:
            try:
                err_data = json.loads(e.read().decode("utf-8"))
                raise APIError(err_data.get("error", str(e)))
            except Exception:
                raise APIError(f"HTTP error {e.code}: {e.reason}")
        except urllib.error.URLError as e:
            raise ConnectionError(f"Failed to connect to Lazerdeck engine: {e.reason}")


class BatchContext:
    """Helper wrapper exposed inside `with engine.batch():` block to mirror deck access."""

    d1: Deck
    d2: Deck
    d3: Deck
    d4: Deck
    d5: Deck
    d6: Deck
    d7: Deck
    d8: Deck
    d9: Deck
    d10: Deck

    def __init__(self, engine: Engine) -> None:
        self._engine = engine

    def __getattr__(self, name: str) -> Any:
        return getattr(self._engine, name)

    def __getitem__(self, item: int) -> Deck:
        return self._engine[item]
