// Widget tests that drive the real engine need lazerdeck_engine.dll next to the
// test runner, which `flutter test` does not provide. The PoC is verified by
// running the app (see docs/08-poc-milestones.md). This placeholder keeps the
// default test suite green.
import 'package:flutter_test/flutter_test.dart';

void main() {
  test('placeholder', () {
    expect(1 + 1, 2);
  });
}
