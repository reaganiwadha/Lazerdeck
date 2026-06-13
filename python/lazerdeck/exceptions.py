class LazerdeckError(Exception):
    """Base exception for all Lazerdeck client library errors."""
    pass


class ValidationError(LazerdeckError, ValueError):
    """Raised when client-side validation of commands fails."""
    pass


class ConnectionError(LazerdeckError, RuntimeError):
    """Raised when connecting to the Lazerdeck engine fails."""
    pass


class APIError(LazerdeckError, RuntimeError):
    """Raised when the Lazerdeck engine returns an error response."""
    pass
