# Basic Deck Example

This example demonstrates how to:
1. Load the `lazerdeck` Python client library directly from its local path without needing to install it first.
2. Initialize the `Engine` client.
3. Check the liveness/health of the audio engine.
4. Retrieve the current status of all decks.
5. Use `engine.batch()` to send volume and EQ adjustments in a single atomic request.

## How to Run

1. Ensure the Lazerdeck audio engine is running and its Control Server is active (usually on port `8203`).
2. Run the example script using Python:

```bash
python main.py
```
