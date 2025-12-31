import { Client } from 'node-osc';

export class DeckProxy {
    private client: Client;
    private deckId: number;

    constructor(id: string | number, host: string = '127.0.0.1', port: number = 9000) {
        this.client = new Client(host, port);
        // Convert 'a', 'b', etc to 0, 1...
        if (typeof id === 'string') {
            this.deckId = id.toLowerCase().charCodeAt(0) - 'a'.charCodeAt(0);
        } else {
            this.deckId = id;
        }
    }

    loadAlways(filepath: string): this {
        this.client.send(`/deck/${this.deckId}/load`, filepath);
        return this;
    }

    load(filepath: string): this {
        return this.loadAlways(filepath);
    }

    using(filepath: string): this {
        this.client.send(`/deck/${this.deckId}/using`, filepath);
        return this;
    }

    play(): this {
        this.client.send(`/deck/${this.deckId}/play`);
        return this;
    }

    pause(): this {
        this.client.send(`/deck/${this.deckId}/pause`);
        return this;
    }

    stop(): this {
        this.client.send(`/deck/${this.deckId}/stop`);
        return this;
    }

    speed(ratio: number): this {
        this.client.send(`/deck/${this.deckId}/speed`, ratio);
        return this;
    }

    stretchBpm(targetBpm: number): this {
        this.client.send(`/deck/${this.deckId}/stretchBpm`, targetBpm);
        return this;
    }

    loopAB(startBeat: number, endBeat: number): this {
        this.client.send(`/deck/${this.deckId}/loopAB`, startBeat, endBeat);
        return this;
    }

    exitLoop(): this {
        this.client.send(`/deck/${this.deckId}/exitLoop`);
        return this;
    }

    setRecognizedBPM(val: number): this {
        this.client.send(`/deck/${this.deckId}/setRecognizedBPM`, val);
        return this;
    }

    bpm(val: number): this {
        return this.setRecognizedBPM(val);
    }

    vstLoad(path: string): this {
        this.client.send(`/deck/${this.deckId}/vst/load`, path);
        return this;
    }

    vstParam(vstIdx: number, paramIdx: number, value: number): this {
        this.client.send(`/deck/${this.deckId}/vst/param`, vstIdx, paramIdx, value);
        return this;
    }

    vstClear(): this {
        this.client.send(`/deck/${this.deckId}/vst/clear`);
        return this;
    }

    offset(val: number): this {
        this.client.send(`/deck/${this.deckId}/offset`, val);
        return this;
    }

    // TidalCycles-like shorthand
    d(id: string | number): DeckProxy {
        return new DeckProxy(id);
    }
}

// Export a factory function like TidalCycles
export const d = (id: string | number) => new DeckProxy(id);

// Example usage:
// d('a').load('resources/znfodastica.wav').play();
// d('a').loopAB(16, 48);
