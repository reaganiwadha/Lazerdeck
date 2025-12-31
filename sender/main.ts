import { d, vst } from "./lazerdeck";

let v = vst('1').using("C:\\Program Files\\Common Files\\VST3\\FabFilter\\FabFilter Pro-R 2.vst3");
let vs = vst('2').using("C:\\Program Files\\Common Files\\VST3\\OTT.vst3");

d('b').using("C:\\Users\\Regan\\Downloads\\fluorite vs goodbye to a world.mp3")
    .setRecognizedBPM(192)
    .stretchBpm(192)
    .loopAB(250, 260)
    .fx(v)
    .play();


// v.show();
