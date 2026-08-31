/**
 * @file LayerManager.js
 * @description Manages the stack of PBR layers in the Vellum engine.
 */

import { Layer } from './Layer.js';

export class LayerManager {
  constructor() {
    /** @type {Layer[]} */
    this.layers = [];
  }

  /**
   * Adds a new layer to the top of the stack.
   * @param {string} id - Unique identifier.
   * @param {string} name - Layer name.
   * @param {Object} maps - Map buffers.
   * @param {number} width - Canvas width.
   * @param {number} height - Canvas height.
   * @returns {Layer} The newly created layer.
   */
  addLayer(id, name, maps, width, height) {
    const layer = new Layer(id, name, maps, width, height);
    this.layers.push(layer);
    return layer;
  }

  /**
   * Removes a layer by ID.
   * @param {string} id 
   */
  removeLayer(id) {
    this.layers = this.layers.filter(l => l.id !== id);
  }

  /**
   * Sets opacity for a layer.
   * @param {string} id 
   * @param {number} opacity 
   */
  setLayerOpacity(id, opacity) {
    const layer = this.getLayer(id);
    if (layer) {
      layer.setOpacity(opacity);
    }
  }

  /**
   * Toggles visibility for a layer.
   * @param {string} id 
   */
  toggleVisibility(id) {
    const layer = this.getLayer(id);
    if (layer) {
      layer.toggleVisibility();
    }
  }

  /**
   * Retrieves a layer by ID.
   * @param {string} id 
   * @returns {Layer|undefined}
   */
  getLayer(id) {
    return this.layers.find(l => l.id === id);
  }

  /**
   * Returns the top visible layer for a specific map type.
   * @param {string} mapType 
   * @returns {import('./Layer').Layer|null}
   */
  getCompositedMap(mapType) {
    // For now, just return the top-most visible layer that has this map.
    for (let i = this.layers.length - 1; i >= 0; i--) {
      const layer = this.layers[i];
      if (layer.visible && layer.getMap(mapType)) {
        return layer;
      }
    }
    return null;
  }
}
