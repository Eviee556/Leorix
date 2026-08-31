/**
 * @file Layer.js
 * @description Represents a single PBR layer in the Vellum engine.
 * Contains multiple map buffers (Albedo, Normal, Roughness, etc.) and metadata.
 */

export class Layer {
  /**
   * @param {string} id - Unique identifier for the layer.
   * @param {string} name - User-facing name.
   * @param {Object} maps - An object containing pixel data buffers for different map types.
   * @param {number} width - Canvas width.
   * @param {number} height - Canvas height.
   */
  constructor(id, name, maps, width, height) {
    this.id = id;
    this.name = name;
    this.width = width;
    this.height = height;

    // Metadata
    this.opacity = 1.0;
    this.visible = true;
    this.blendingMode = 'normal'; // Default blending mode

    // Map buffers
    // Expected map types: 'albedo', 'normal', 'roughness', 'metallic', 'ao', 'displacement'
    // Maps are stored as TypedArrays for high precision manipulation.
    this.maps = {
      albedo: maps.albedo || new Uint8ClampedArray(width * height * 4),
      normal: maps.normal || new Float32Array(width * height * 3),
      roughness: maps.roughness || new Float32Array(width * height),
      metallic: maps.metallic || new Float32Array(width * height),
      ao: maps.ao || new Float32Array(width * height),
      displacement: maps.displacement || new Float32Array(width * height),
    };
  }

  /**
   * Updates the opacity of the layer.
   * @param {number} opacity - Value between 0.0 and 1.0.
   */
  setOpacity(opacity) {
    this.opacity = Math.max(0, Math.min(1, opacity));
  }

  /**
   * Toggles the visibility of the layer.
   */
  toggleVisibility() {
    this.visible = !this.visible;
  }

  /**
   * Gets a specific map buffer.
   * @param {string} mapType - The type of map to retrieve.
   * @returns {TypedArray|null}
   */
  getMap(mapType) {
    return this.maps[mapType] || null;
  }
}
