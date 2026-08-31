/**
 * @file CommandManager.js
 * @description Implements the Command Pattern for undo/redo functionality.
 */

export class CommandManager {
  constructor() {
    /** @type {Array<Object>} */
    this.undoStack = [];
    /** @type {Array<Object>} */
    this.redoStack = [];
  }

  /**
   * Executes a command and adds it to the undo stack.
   * @param {Object} command - An object with execute() and undo() methods.
   */
  execute(command) {
    try {
      command.execute();
      this.undoStack.push(command);
      // Clear redo stack on new command execution
      this.redoStack = [];
    } catch (error) {
      console.error("Failed to execute command:", error);
    }
  }

  /**
   * Undoes the last executed command.
   */
  undo() {
    if (this.undoStack.length === 0) return;

    const command = this.undoStack.pop();
    try {
      command.undo();
      this.redoStack.push(command);
    } catch (error) {
      console.error("Failed to undo command:", error);
    }
  }

  /**
   * Redoes the last undone command.
   */
  redo() {
    if (this.redoStack.length === 0) return;

    const command = this.redoStack.pop();
    try {
      command.execute();
      this.undoStack.push(command);
    } catch (error) {
      console.error("Failed to redo command:", error);
    }
  }

  /**
   * Returns the number of items in the undo stack.
   * @returns {number}
   */
  getUndoCount() {
    return this.undoStack.length;
  }

  /**
   * Returns the number of items in the redo stack.
   * @returns {number}
   */
  getRedoCount() {
    return this.redoStack.length;
  }
}
