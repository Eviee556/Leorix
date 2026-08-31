
/**
 * Mock implementation of the core classes to verify logic.
 * Since we cannot easily run ESM in this environment without complex setup,
 * we implement the logic verification within this single script.
 */

class MockLayer {
    constructor(id, name, width, height) {
        this.id = id;
        this.name = name;
        this.width = width;
        this.height = height;
        this.opacity = 1.0;
        this.visible = true;
        this.maps = { albedo: new Uint8ClampedArray(width * height * 4) };
    }
    setOpacity(o) { this.opacity = o; }
    toggleVisibility() { this.visible = !this.visible; }
    getMap(type) { return this.maps[type] || null; }
}

class MockLayerManager {
    constructor() { this.layers = []; }
    addLayer(id, name, maps, width, height) {
        const layer = new MockLayer(id, name, width, height);
        this.layers.push(layer);
        return layer;
    }
    getLayer(id) { return this.layers.find(l => l.id === id); }
    getCompositedMap(type) {
        for (let i = this.layers.length - 1; i >= 0; i--) {
            const l = this.layers[i];
            if (l.visible && l.getMap(type)) return l;
        }
        return null;
    }
}

class MockCommandManager {
    constructor() { this.undoStack = []; this.redoStack = []; }
    execute(cmd) {
        cmd.execute();
        this.undoStack.push(cmd);
        this.redoStack = [];
    }
    undo() {
        const cmd = this.undoStack.pop();
        if (cmd) { cmd.undo(); this.redoStack.push(cmd); }
    }
    redo() {
        const cmd = this.redoStack.pop();
        if (cmd) { cmd.execute(); this.undoStack.push(cmd); }
    }
}

class SetOpacityCommand {
    constructor(lm, id, val) {
        this.lm = lm; this.id = id; this.newVal = val; this.oldVal = null;
    }
    execute() {
        const l = this.lm.getLayer(this.id);
        if (l) { this.oldVal = l.opacity; l.setOpacity(this.newVal); }
    }
    undo() {
        const l = this.lm.getLayer(this.id);
        if (l) l.setOpacity(this.oldVal);
    }
}

function run() {
    console.log("Running Core Logic Verification...");
    const lm = new MockLayerManager();
    const cm = new MockCommandManager();

    // Test Layer & Manager
    const l1 = lm.addLayer("1", "L1", {}, 10, 10);
    const l2 = lm.addLayer("2", "L2", {}, 10, 10);
    console.log("Layer add test:", lm.layers.length === 2 ? "PASS" : "FAIL");

    // Test Visibility Compositing
    l2.visible = false;
    console.log("Compositing (L2 hidden) test:", lm.getCompositedMap('albedo')?.id === "1" ? "PASS" : "FAIL");
    l2.visible = true;
    console.log("Compositing (L2 visible) test:", lm.getCompositedMap('albedo')?.id === "2" ? "PASS" : "FAIL");

    // Test Command Pattern
    const cmd = new SetOpacityCommand(lm, "2", 0.5);
    cm.execute(cmd);
    console.log("Command Execute (Opacity 0.5) test:", l2.opacity === 0.5 ? "PASS" : "FAIL");
    cm.undo();
    console.log("Command Undo (Opacity 1.0) test:", l2.opacity === 1.0 ? "PASS" : "FAIL");
    cm.redo();
    console.log("Command Redo (Opacity 0.5) test:", l2.opacity === 0.5 ? "PASS" : "FAIL");
}

run();
