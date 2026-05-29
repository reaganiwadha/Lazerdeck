// Album-art loading + display for each deck. Cover bytes are resolved on a
// background isolate (embedded ID3/FLAC/MP4 picture first, then a sidecar image
// like album.jpg in the same folder). When nothing is found, a black→white
// diagonal gradient stands in. Also provides the stroked "Deck A/B" label.
import 'dart:io';

import 'package:audio_metadata_reader/audio_metadata_reader.dart';
import 'package:flutter/foundation.dart';
import 'package:flutter/material.dart';

const _kImageExts = {'.jpg', '.jpeg', '.png', '.webp', '.bmp', '.gif'};
// Sidecar filenames (without extension) to prefer, in priority order.
const _kPreferredNames = [
  'cover',
  'folder',
  'front',
  'album',
  'albumart',
  'artwork',
];

/// Cover art + title/artist tags for one track. Sendable across isolates.
class TrackMeta {
  final Uint8List? cover;
  final String? title;
  final String? artist;
  const TrackMeta({this.cover, this.title, this.artist});
}

/// Resolves cover art and title/artist tags for [path]. Pure/blocking — meant
/// to run via [compute] on a background isolate.
TrackMeta loadTrackMetaSync(String path) {
  if (path.isEmpty) return const TrackMeta();

  Uint8List? cover;
  String? title;
  String? artist;

  // Embedded tag (ID3v2, FLAC/MP4/OGG comments + picture block).
  try {
    final file = File(path);
    if (file.existsSync()) {
      final md = readMetadata(file, getImage: true);
      title = _clean(md.title);
      artist = _clean(md.artist);
      if (md.pictures.isNotEmpty) {
        // Prefer the tagged front cover, else just the first picture.
        final pic = md.pictures.firstWhere(
          (p) => p.pictureType == PictureType.coverFront,
          orElse: () => md.pictures.first,
        );
        // audio_metadata_reader mis-parses UTF-16 APIC descriptions and leaves
        // leftover text bytes before the actual image, so re-anchor to the real
        // image header. Also yields a compact, transfer-safe copy.
        final raw = pic.bytes;
        final start = _findImageStart(raw);
        cover = Uint8List.fromList(start == 0 ? raw : raw.sublist(start));
      }
    }
  } catch (_) {
    // Corrupt/unsupported tag — keep whatever we parsed, fall back for the art.
  }

  // Fall back to a sidecar image in the same folder (album.jpg, cover.png, …).
  cover ??= _folderCover(path);

  return TrackMeta(cover: cover, title: title, artist: artist);
}

Uint8List? _folderCover(String path) {
  try {
    final dir = File(path).parent;
    if (dir.existsSync()) {
      final images = <File>[
        for (final e in dir.listSync())
          if (e is File && _kImageExts.contains(_ext(e.path))) e,
      ];
      if (images.isNotEmpty) {
        for (final name in _kPreferredNames) {
          for (final img in images) {
            if (_stem(img.path).toLowerCase() == name) {
              return img.readAsBytesSync();
            }
          }
        }
        // No well-known name — take the first image in the folder.
        return images.first.readAsBytesSync();
      }
    }
  } catch (_) {}
  return null;
}

String? _clean(String? s) {
  final t = s?.trim();
  return (t == null || t.isEmpty) ? null : t;
}

/// Returns the offset of the first known image-format signature within [b]
/// (JPEG/PNG/GIF/BMP/WebP), or 0 if none is found in the leading bytes. Used to
/// strip junk that broken tag parsers prepend to embedded picture data.
int _findImageStart(Uint8List b) {
  for (var i = 0; i + 3 < b.length && i < 4096; i++) {
    final a = b[i], c = b[i + 1], d = b[i + 2], e = b[i + 3];
    if (a == 0xFF && c == 0xD8 && d == 0xFF) return i; // JPEG
    if (a == 0x89 && c == 0x50 && d == 0x4E && e == 0x47) return i; // PNG
    if (a == 0x47 && c == 0x49 && d == 0x46 && e == 0x38) return i; // GIF8
    if (a == 0x42 && c == 0x4D) return i; // BMP "BM"
    if (a == 0x52 && c == 0x49 && d == 0x46 && e == 0x46) return i; // RIFF/WebP
  }
  return 0;
}

String _ext(String p) {
  final i = p.lastIndexOf('.');
  return i < 0 ? '' : p.substring(i).toLowerCase();
}

String _stem(String p) {
  final slash = p.lastIndexOf(RegExp(r'[\\/]'));
  final name = slash >= 0 ? p.substring(slash + 1) : p;
  final dot = name.lastIndexOf('.');
  return dot < 0 ? name : name.substring(0, dot);
}

/// Cover tile + track title/artist for one deck. Loads tags once per track on a
/// background isolate. Shows tagged title/artist when present, otherwise the
/// [fallbackName] (filename). The text auto-shrinks to fit the available width.
class DeckTrackHeader extends StatefulWidget {
  final String? path; // null when no track is loaded
  final String fallbackName; // shown when there's no title tag
  final double coverSize;

  const DeckTrackHeader({
    super.key,
    required this.path,
    required this.fallbackName,
    this.coverSize = 54,
  });

  @override
  State<DeckTrackHeader> createState() => _DeckTrackHeaderState();
}

class _DeckTrackHeaderState extends State<DeckTrackHeader> {
  String? _loadedFor;
  TrackMeta _meta = const TrackMeta();
  int _reqId = 0; // guards against an older async load winning a race

  @override
  void initState() {
    super.initState();
    _maybeLoad();
  }

  @override
  void didUpdateWidget(DeckTrackHeader old) {
    super.didUpdateWidget(old);
    if (old.path != widget.path) _maybeLoad();
  }

  Future<void> _maybeLoad() async {
    final path = widget.path;
    if (path == _loadedFor) return;
    _loadedFor = path;
    final req = ++_reqId;

    // Drop the previous track's metadata immediately so it doesn't linger.
    if (mounted) setState(() => _meta = const TrackMeta());

    if (path == null || path.isEmpty) return;

    final meta = await compute(loadTrackMetaSync, path);
    if (!mounted || req != _reqId) return; // superseded by a newer load
    setState(() => _meta = meta);
  }

  @override
  Widget build(BuildContext context) {
    final hasTrack = widget.path != null && widget.path!.isNotEmpty;
    final title =
        hasTrack ? (_meta.title ?? widget.fallbackName) : 'No track loaded';
    final artist = hasTrack ? _meta.artist : null;

    return Row(
      children: [
        _Cover(bytes: _meta.cover, size: widget.coverSize),
        const SizedBox(width: 16),
        Expanded(
          child: Column(
            mainAxisSize: MainAxisSize.min,
            mainAxisAlignment: MainAxisAlignment.center,
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              if (artist != null)
                _AutoFitLine(
                  text: artist,
                  maxFontSize: 15,
                  color: Colors.white54,
                ),
              _AutoFitLine(
                text: title,
                maxFontSize: 24,
                color: hasTrack ? Colors.white : Colors.white38,
              ),
            ],
          ),
        ),
      ],
    );
  }
}

/// A single line of text that scales down (never up) to fit its width.
class _AutoFitLine extends StatelessWidget {
  final String text;
  final double maxFontSize;
  final Color color;

  const _AutoFitLine({
    required this.text,
    required this.maxFontSize,
    required this.color,
  });

  @override
  Widget build(BuildContext context) {
    return SizedBox(
      width: double.infinity,
      child: FittedBox(
        fit: BoxFit.scaleDown,
        alignment: Alignment.centerLeft,
        child: Text(
          text,
          maxLines: 1,
          softWrap: false,
          style: TextStyle(
            fontSize: maxFontSize,
            height: 1.15,
            fontFamily: 'bitroad',
            fontWeight: FontWeight.w500,
            color: color,
          ),
        ),
      ),
    );
  }
}

/// Square cover-art tile, or the diagonal placeholder when [bytes] is null.
class _Cover extends StatelessWidget {
  final Uint8List? bytes;
  final double size;
  const _Cover({required this.bytes, required this.size});

  @override
  Widget build(BuildContext context) {
    final dpr = MediaQuery.maybeOf(context)?.devicePixelRatio ?? 1.0;
    return ClipRRect(
      borderRadius: BorderRadius.circular(6),
      child: SizedBox(
        width: size,
        height: size,
        child: bytes != null
            ? Image.memory(
                bytes!,
                width: size,
                height: size,
                fit: BoxFit.cover,
                gaplessPlayback: true,
                filterQuality: FilterQuality.medium,
                // Decode only as large as we draw, to bound memory/decode cost.
                cacheWidth: (size * dpr).round(),
                // Undecodable bytes fall back to the placeholder, never a red X.
                errorBuilder: (_, _, _) => const _DiagonalPlaceholder(),
              )
            : const _DiagonalPlaceholder(),
      ),
    );
  }
}

/// Black→white diagonal gradient stand-in for missing cover art.
class _DiagonalPlaceholder extends StatelessWidget {
  const _DiagonalPlaceholder();

  @override
  Widget build(BuildContext context) {
    return const DecoratedBox(
      decoration: BoxDecoration(
        gradient: LinearGradient(
          begin: Alignment.topLeft,
          end: Alignment.bottomRight,
          colors: [Colors.black, Colors.white],
        ),
      ),
    );
  }
}

/// Text with an outline stroke plus a soft drop shadow, so it stays legible
/// when laid over a busy waveform.
class StrokedText extends StatelessWidget {
  final String text;
  final double fontSize;
  final String? fontFamily;
  final FontWeight fontWeight;
  final double letterSpacing;
  final double strokeWidth;
  final Color color;
  final Color strokeColor;

  const StrokedText(
    this.text, {
    super.key,
    this.fontSize = 16,
    this.fontFamily,
    this.fontWeight = FontWeight.w700,
    this.letterSpacing = 0,
    this.strokeWidth = 3,
    this.color = Colors.white,
    this.strokeColor = Colors.black,
  });

  @override
  Widget build(BuildContext context) {
    return Stack(
      children: [
        // Outline underneath.
        Text(
          text,
          style: TextStyle(
            fontSize: fontSize,
            fontFamily: fontFamily,
            fontWeight: fontWeight,
            letterSpacing: letterSpacing,
            foreground: Paint()
              ..style = PaintingStyle.stroke
              ..strokeWidth = strokeWidth
              ..strokeJoin = StrokeJoin.round
              ..color = strokeColor,
          ),
        ),
        // Solid fill + shadow on top.
        Text(
          text,
          style: TextStyle(
            fontSize: fontSize,
            fontFamily: fontFamily,
            fontWeight: fontWeight,
            letterSpacing: letterSpacing,
            color: color,
            shadows: const [Shadow(blurRadius: 4, color: Colors.black87)],
          ),
        ),
      ],
    );
  }
}
