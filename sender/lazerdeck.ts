import { Client } from 'node-osc';

// Global client for all proxies
let globalClient: Client | null = null;
const getClient = () => {
    if (!globalClient) {
        globalClient = new Client('127.0.0.1', 9000);
    }
    return globalClient;
};

export class VSTProxy {
    public path: string = "";
    public deckId: number | null = null;
    public index: number;

    constructor(public id: string) {
        // Assume id is '1', '2', etc.
        this.index = parseInt(id) - 1;
        if (isNaN(this.index)) this.index = 0;
    }

    using(path: string): this {
        this.path = path;
        if (this.deckId !== null) {
            getClient().send(`/deck/${this.deckId}/vst/${this.index}/using`, this.path);
        }
        return this;
    }

    load(path: string): this {
        return this.using(path);
    }

    show(): this {
        if (this.deckId !== null) {
            getClient().send(`/deck/${this.deckId}/vst/${this.index}/show`);
        }
        return this;
    }

    set(param: number, value: number): this {
        if (this.deckId !== null) {
            getClient().send(`/deck/${this.deckId}/vst/${this.index}/param`, param, value);
        }
        return this;
    }
}

export class DeckProxy {
    private deckId: number;
    private vstChain: Map<number, VSTProxy> = new Map();

    constructor(id: string | number) {
        // Convert 'a', 'b', etc to 0, 1...
        if (typeof id === 'string') {
            const firstChar = id.toLowerCase().charAt(0);
            if (firstChar >= 'a' && firstChar <= 'z') {
                this.deckId = firstChar.charCodeAt(0) - 'a'.charCodeAt(0);
            } else {
                this.deckId = parseInt(id) - 1; // 1-indexed string fallback
            }
        } else {
            this.deckId = id;
        }
    }

    loadAlways(filepath: string): this {
        getClient().send(`/deck/${this.deckId}/load`, filepath);
        return this;
    }

    load(filepath: string): this {
        return this.loadAlways(filepath);
    }

    using(filepath: string): this {
        getClient().send(`/deck/${this.deckId}/using`, filepath);
        return this;
    }

    play(): this {
        getClient().send(`/deck/${this.deckId}/play`);
        return this;
    }

    pause(): this {
        getClient().send(`/deck/${this.deckId}/pause`);
        return this;
    }

    stop(): this {
        getClient().send(`/deck/${this.deckId}/stop`);
        return this;
    }

    speed(ratio: number): this {
        getClient().send(`/deck/${this.deckId}/speed`, ratio);
        return this;
    }

    stretchBpm(targetBpm: number): this {
        getClient().send(`/deck/${this.deckId}/stretchBpm`, targetBpm);
        return this;
    }

    loopAB(startBeat: number, endBeat: number): this {
        getClient().send(`/deck/${this.deckId}/loopAB`, startBeat, endBeat);
        return this;
    }

    exitLoop(): this {
        getClient().send(`/deck/${this.deckId}/exitLoop`);
        return this;
    }

    setRecognizedBPM(val: number): this {
        getClient().send(`/deck/${this.deckId}/setRecognizedBPM`, val);
        return this;
    }

    bpm(val: number): this {
        return this.setRecognizedBPM(val);
    }

    fx(vst: VSTProxy): this {
        vst.deckId = this.deckId;
        this.vstChain.set(vst.index, vst);
        getClient().send(`/deck/${this.deckId}/vst/${vst.index}/using`, vst.path);
        return this;
    }

    vstLoad(path: string): this {
        // Legacy/Direct
        getClient().send(`/deck/${this.deckId}/vst/load`, path);
        return this;
    }

    vstParam(vstIdx: number, paramIdx: number, value: number): this {
        getClient().send(`/deck/${this.deckId}/vst/param`, vstIdx, paramIdx, value);
        return this;
    }

    vstClear(): this {
        this.vstChain.clear();
        getClient().send(`/deck/${this.deckId}/vst/clear`);
        return this;
    }

    offset(val: number): this {
        getClient().send(`/deck/${this.deckId}/offset`, val);
        return this;
    }

    // TidalCycles-like shorthand
    d(id: string | number): DeckProxy {
        return new DeckProxy(id);
    }
}

// Export factory functions
export const d = (id: string | number) => new DeckProxy(id);
export const vst = (id: string) => new VSTProxy(id);

// Example usage:
// let v = vst('1').load('path/to/vst');
// d('a').load('track.wav').fx(v).play();
// v.show();
