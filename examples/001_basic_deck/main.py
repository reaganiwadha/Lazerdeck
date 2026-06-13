# /// script
# dependencies = [
#   "lazerdeck",
# ]
# [tool.uv.sources]
# lazerdeck = { path = "../../python" }
# ///

import os
import sys
import time

try:
    from lazerdeck import Engine, ValidationError, ConnectionError, APIError
except ImportError:
    # Add the python/ directory to sys.path to load 'lazerdeck' by path relative to this script
    try:
        script_dir = os.path.dirname(os.path.abspath(__file__))
        python_path = os.path.abspath(os.path.join(script_dir, "..", "..", "python"))
    except NameError:
        # If __file__ is not defined, we might be in an interactive REPL/console.
        # Try using the current working directory as a starting point.
        cwd = os.getcwd()
        if os.path.exists(os.path.join(cwd, "..", "..", "python")):
            python_path = os.path.abspath(os.path.join(cwd, "..", "..", "python"))
        elif os.path.exists(os.path.join(cwd, "python")):
            python_path = os.path.abspath(os.path.join(cwd, "python"))
        else:
            python_path = cwd

    sys.path.insert(0, python_path)
    try:
        from lazerdeck import Engine, ValidationError, ConnectionError, APIError
    except ImportError as e:
        print(f"Error importing lazerdeck: {e}")
        sys.exit(1)

def main():
    print("=" * 60)
    print(" Lazerdeck Python Library — Basic Deck Example")
    print("=" * 60)

    # Initialize the engine client
    engine = Engine()
    print(f"Connecting to control server at: {engine.url}")

    # Check health
    if not engine.health():
        print("[!] Engine is not running or control server is disabled.")
        print("    Please start Lazerdeck (e.g., flutter run -d windows in 'app')")
        print("    and make sure the Control Server is active (default port 8203).")
        print("=" * 60)
        sys.exit(1)

    print("[+] Engine is online!")

    # Retrieve and print initial state
    try:
        states = engine.get_state()
        print(f"[+] Connected decks: {len(states)}")
        for state in states:
            print(f"    - {state.deck}: playing={state.is_playing}, volume={state.volume}, file='{state.filepath}'")
    except APIError as e:
        print(f"[-] Failed to fetch deck state: {e}")

    print("\n--- Sending Commands ---")

    # Load and play deck 1
    # Note: Forward slashes work on Windows and are standard for the engine path
    dummy_track = "C:/music/demo.wav"
    print(f"[1] Queueing track load: {dummy_track} on deck 1")

    # In a real environment, you'd load a real track. We wrap this in try/except.
    # We will use engine.batch to group commands together.
    print("[2] Batching volume setup and play for deck 1...")
    try:
        with engine.batch():
            # These will be bundled into a single HTTP POST request
            engine.d1.volume(0.8)
            engine.d1.eq_mid(0.5)
            engine.d1.eq_low(0.5)
            # You can uncomment these to try with a real file:
            # engine.d1.load(dummy_track)
            # engine.d1.play()
        print("[+] Batch action sent successfully!")
    except ValidationError as e:
        print(f"[-] Client validation error: {e}")
    except ConnectionError as e:
        print(f"[-] Connection error: {e}")
    except APIError as e:
        print(f"[-] Engine error: {e}")

    # Inspect the updated state
    try:
        states = engine.get_state()
        print("\n--- Updated Deck States ---")
        for state in states:
            print(f"    - {state.deck}: volume={state.volume}, EQ low/mid/high={state.eq_low}/{state.eq_mid}/{state.eq_high}")
    except APIError as e:
        print(f"[-] Failed to fetch state: {e}")

    print("=" * 60)
    print(" Done!")
    print("=" * 60)


if __name__ == "__main__":
    main()

