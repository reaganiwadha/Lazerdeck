import { d, vst, s } from "./lazerdeck";

s().workingDirectory("C:\\Users\\Regan\\Downloads");
s().printVSTs();

let v = vst('1').using("C:\\Program Files\\Common Files\\VST3\\FabFilter\\FabFilter Pro-Q 4.vst3");
// let vs = vst('2').using("C:\\Program Files\\Common Files\\VST3\\OTT.vst3");

d('a').play().syncTo(d('b')).syncBpmTo(d('b')).beatSync(d('b'))

d('b').using("C:\\Users\\Regan\\Downloads\\fluorite vs goodbye to a world.mp3")
    .setRecognizedBPM(192)
    .stretchBpm(192)
    .loopAB(250, 260)
    .fx(v)
    .atBeat(49, (e) => {
        e.d('a').jumpToBeat(75).play().syncTo(d('b'));
    })

    .pause()
    .play();


v.show();
