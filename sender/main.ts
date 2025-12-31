import { DeckProxy } from "./lazerdeck";

const d = new DeckProxy('b');

d.using("C:\\Users\\Regan\\Downloads\\fluorite vs goodbye to a world.mp3")
    .setRecognizedBPM(192)
    .stretchBpm(192)
    .vstLoad("C:\\Program Files\\Common Files\\VST3\\FabFilter\\FabFilter Pro-R 2.vst3")
    .play()
// d.play()
// d.loopAB(128, 128);
// d.bpm(20);
