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
    public deckId: string | null = null; // Changed to string to match DeckProxy.id
    public index: number;

    constructor(public id: string) {
        // Assume id is '1', '2', etc.
        this.index = parseInt(id) - 1;
        if (isNaN(this.index)) this.index = 0;
    }

    using(path: string): this {
        this.path = path;
        if (this.deckId !== null) {
            // Helper to parse deck ID locally since VSTProxy doesn't inherit DeckProxy
            let idx = 0;
            const oid = this.deckId.toLowerCase();
            if (oid === 'a' || oid === '1') idx = 0;
            else if (oid === 'b' || oid === '2') idx = 1;
            else idx = parseInt(oid) - 1;
            if (isNaN(idx)) idx = 0;

            getClient().send(`/deck/${idx}/vst/${this.index}/using`, this.path);
        }
        return this;
    }

    load(path: string): this {
        return this.using(path);
    }

    show(): this {
        if (this.deckId !== null) {
             let idx = 0;
            const oid = this.deckId.toLowerCase();
            if (oid === 'a' || oid === '1') idx = 0;
            else if (oid === 'b' || oid === '2') idx = 1;
            else idx = parseInt(oid) - 1;
            if (isNaN(idx)) idx = 0;
            getClient().send(`/deck/${idx}/vst/${this.index}/show`);
        }
        return this;
    }

    set(param: number, value: number): this {
        if (this.deckId !== null) {
             let idx = 0;
            const oid = this.deckId.toLowerCase();
            if (oid === 'a' || oid === '1') idx = 0;
            else if (oid === 'b' || oid === '2') idx = 1;
            else idx = parseInt(oid) - 1;
            if (isNaN(idx)) idx = 0;
            getClient().send(`/deck/${idx}/vst/${this.index}/param`, param, value);
        }
        return this;
    }
}

export class DeckProxy {
    private isRecording: boolean = false;
    private recordedActions: any[] = [];
    private pendingActions: (() => void)[] = [];

    constructor(public id: string) {}

    // Executes the chain
    private commit() {
        if (this.isRecording) return; // Don't commit if we are inside an atBeat callback

        // 1. Begin Definition
        getClient().send(`/deck/${this.idx}/define/begin`);

        // 2. Execute all queued commands
        this.pendingActions.forEach(a => a());
        this.pendingActions = [];

        // 3. End Definition (Cleans up unused VSTs/Triggers)
        getClient().send(`/deck/${this.idx}/define/end`);
    }

    // Helper to queue or send
    private queue(action: () => void) {
        if (this.isRecording) {
            // If recording, we can't execute immediately. We assume recording captures "Intent".
            // But wait, "atBeat" uses a separate "recording" proxy.
            // This 'queue' is for the MAIN chain.
            action();
        } else {
            this.pendingActions.push(action);
        }
    }

    private get idx(): number {
        return this.parseDeckId(this.id);
    }

    // Terminators (Execute the chain)
    play(): this {
        this.queue(() => getClient().send(`/deck/${this.idx}/play`));
        this.commit();
        return this;
    }

    pause(): this {
        this.queue(() => getClient().send(`/deck/${this.idx}/pause`));
        this.commit();
        return this;
    }
    
    stop(): this {
        this.queue(() => getClient().send(`/deck/${this.idx}/stop`));
        this.commit();
        return this;
    }

    // Explicit applier if not playing/stopping
    apply(): this {
        this.commit();
        return this;
    }

    // Properties (Queued)
    using(path: string): this {
        this.queue(() => getClient().send(`/deck/${this.idx}/using`, path));
        return this;
    }

    load(path: string): this {
        return this.using(path);
    }

    speed(val: number): this {
        this.queue(() => getClient().send(`/deck/${this.idx}/speed`, val));
        return this;
    }

    setRecognizedBPM(bpm: number): this {
        this.queue(() => getClient().send(`/deck/${this.idx}/setRecognizedBPM`, bpm));
        return this;
    }

    stretchBpm(targetBpm: number): this {
        this.queue(() => getClient().send(`/deck/${this.idx}/stretchBpm`, targetBpm));
        return this;
    }

    loopAB(start: number, end: number): this {
        this.queue(() => getClient().send(`/deck/${this.idx}/loopAB`, start, end));
        return this;
    }
    
    jumpToBeat(beat: number): this {
        this.queue(() => getClient().send(`/deck/${this.idx}/jumpToBeat`, beat));
        return this;
    }

    syncTo(otherDeck: DeckProxy): this {
        let otherIdx = this.parseDeckId(otherDeck.id);
        this.queue(() => getClient().send(`/deck/${this.idx}/syncTo`, otherIdx));
        return this;
    }

    beatSync(otherDeck: DeckProxy): this {
        let otherIdx = this.parseDeckId(otherDeck.id);
        this.queue(() => getClient().send(`/deck/${this.idx}/beatSync`, otherIdx));
        return this;
    }

    syncBpmTo(otherDeck: DeckProxy): this { return this.syncTo(otherDeck); }

    syncOff(): this {
        this.queue(() => getClient().send(`/deck/${this.idx}/syncOff`));
        return this;
    }
    
    // VST
    private vstChainCount: number = 0;

    fx(vst: VSTProxy): this {
        // In the new model, we assign indices sequentially based on chain order
        // to match the "Re-define clears old" logic.
        const myIndex = this.vstChainCount++;
        
        this.queue(() => {
            getClient().send(`/deck/${this.idx}/vst/${myIndex}/using`, vst.path);
        });
        
        // Reset count for next run? 
        // No, pendingActions are executed on commit. 
        // We need to reset vstChainCount when? 
        // When we start building a new chain.
        // `d('a')` -> new DeckProxy -> count = 0. Correct.
        return this;
    }

    // Triggers
    atBeat(beat: number, callback: (e: { d: (id: string) => DeckProxy }) => void): this {
        this.queue(() => {
            const triggerId = Math.floor(Math.random() * 1000000);
            getClient().send(`/deck/${this.idx}/trigger/new`, triggerId, beat);

            const capturedActions: any[] = [];
            const recordingContext = {
                d: (id: string) => {
                    const proxy = new DeckProxy(id);
                    proxy.isRecording = true; // Mark as recording
                    proxy.recordedActions = capturedActions;
                    return proxy;
                }
            };

            callback(recordingContext);

            capturedActions.forEach(action => {
                let targetIdx = 0;
                const tid = action.target.toLowerCase();
                if (tid === 'a' || tid === '1') targetIdx = 0;
                else if (tid === 'b' || tid === '2') targetIdx = 1;
                else targetIdx = parseInt(tid) - 1;
                if (isNaN(targetIdx)) targetIdx = 0;
                
                getClient().send(`/deck/${this.idx}/trigger/action`, triggerId, targetIdx, action.cmd, ...action.args);
            });
        });
        return this;
    }

    private parseDeckId(id: string): number {
        let idx = 0;
        const oid = id.toLowerCase();
        if (oid === 'a' || oid === '1') idx = 0;
        else if (oid === 'b' || oid === '2') idx = 1;
        else idx = parseInt(oid) - 1;
        if (isNaN(idx)) idx = 0;
        return idx;
    }
}

export class SystemProxy {
    printVSTs(): void {
        getClient().send(`/system/printVSTs`);
    }

    workingDirectory(dir: string): void {
        getClient().send(`/system/workingDirectory`, dir);
    }
}

// Export factory functions
export const d = (id: string) => new DeckProxy(id);
export const vst = (id: string) => new VSTProxy(id);
export const s = () => new SystemProxy();

// Example usage:
// let v = vst('1').load('path/to/vst');
// d('a').load('track.wav').fx(v).play();
// v.show();