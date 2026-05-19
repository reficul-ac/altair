(function () {
  class VehicleModelController {
    constructor(cesium) {
      this.Cesium = cesium || window.Cesium;
      this.profile = null;
      this.surfaces = new Map();
      this.model = null;
      this.nodeStates = new Map();
      this.warned = new Set();
    }

    warnOnce(key, message) {
      if (this.warned.has(key)) return;
      this.warned.add(key);
      console.warn(`Animus vehicle model: ${message}`);
    }

    setProfile(profile) {
      this.profile = profile || null;
      this.surfaces.clear();
      this.nodeStates.clear();
      const surfaces = Array.isArray(profile && profile.surfaces) ? profile.surfaces : [];
      surfaces.forEach((surface) => {
        if (!surface || !surface.id || !surface.node) return;
        this.surfaces.set(String(surface.id), surface);
      });
      if (!profile) {
        this.warnOnce('profile-missing', 'profile data is not available');
      }
    }

    setModel(model) {
      if (this.model === model) return;
      this.model = model || null;
      this.nodeStates.clear();
      if (!this.model) {
        this.warnOnce('model-missing', 'Cesium model instance is not available');
      }
    }

    axisFor(surface) {
      const Cesium = this.Cesium;
      const axis = Array.isArray(surface.axis) ? surface.axis : [1.0, 0.0, 0.0];
      const vector = new Cesium.Cartesian3(
        Number(axis[0]) || 0.0,
        Number(axis[1]) || 0.0,
        Number(axis[2]) || 0.0
      );
      if (Cesium.Cartesian3.magnitude(vector) <= 0.0) {
        return Cesium.Cartesian3.UNIT_X;
      }
      return Cesium.Cartesian3.normalize(vector, vector);
    }

    resolveNode(surface) {
      if (!this.model || !surface || !surface.node) return null;
      const nodeName = String(surface.node);
      if (this.nodeStates.has(nodeName)) return this.nodeStates.get(nodeName);
      if (typeof this.model.getNode !== 'function') {
        this.warnOnce('model-get-node-missing', 'Cesium model does not expose getNode(name)');
        return null;
      }

      const node = this.model.getNode(nodeName);
      if (!node) {
        this.warnOnce(`node-missing:${nodeName}`, `glTF node '${nodeName}' was not found`);
        return null;
      }

      const Cesium = this.Cesium;
      const originalMatrix = Cesium.Matrix4.clone(
        node.matrix || Cesium.Matrix4.IDENTITY,
        new Cesium.Matrix4()
      );
      const state = {node, originalMatrix};
      this.nodeStates.set(nodeName, state);
      return state;
    }

    applyControlSurfaces(controlSurfaces) {
      if (!this.Cesium) {
        this.warnOnce('cesium-missing', 'Cesium runtime is not available');
        return;
      }
      if (!this.profile) {
        this.warnOnce('profile-not-loaded', 'cannot apply surfaces before a profile is loaded');
        return;
      }
      if (!this.model) {
        this.warnOnce('model-not-set', 'cannot resolve control-surface nodes before a model exists');
        return;
      }
      const Cesium = this.Cesium;
      const updates = Array.isArray(controlSurfaces) ? controlSurfaces : [];
      updates.forEach((update) => {
        const surface = update && update.id ? this.surfaces.get(String(update.id)) : null;
        if (!surface) {
          this.warnOnce(`surface-missing:${update && update.id}`, 'snapshot references an unknown surface');
          return;
        }
        const nodeState = this.resolveNode(surface);
        if (!nodeState) return;
        const deflectionDeg = Number(update.deflectionDeg);
        const angle = Number.isFinite(deflectionDeg) ? Cesium.Math.toRadians(deflectionDeg) : 0.0;
        const rotation = Cesium.Matrix3.fromQuaternion(
          Cesium.Quaternion.fromAxisAngle(this.axisFor(surface), angle),
          new Cesium.Matrix3()
        );
        const rotationMatrix = Cesium.Matrix4.fromRotationTranslation(
          rotation,
          Cesium.Cartesian3.ZERO,
          new Cesium.Matrix4()
        );
        nodeState.node.matrix = Cesium.Matrix4.multiply(
          nodeState.originalMatrix,
          rotationMatrix,
          new Cesium.Matrix4()
        );
      });
    }

    matricesDiffer(left, right) {
      if (!left || !right) return false;
      for (let index = 0; index < 16; ++index) {
        if (Math.abs(Number(left[index]) - Number(right[index])) > 1.0e-9) {
          return true;
        }
      }
      return false;
    }

    inspectControlSurfaces(controlSurfaces) {
      const surfaces = [];
      const failures = [];
      if (!this.Cesium) {
        failures.push('Cesium runtime is not available');
      }
      if (!this.profile) {
        failures.push('vehicle model profile is not loaded');
      }
      if (!this.model) {
        failures.push('vehicle model primitive is not loaded');
      }

      const updatesById = new Map();
      const updates = Array.isArray(controlSurfaces) ? controlSurfaces : [];
      updates.forEach((update) => {
        if (update && update.id) updatesById.set(String(update.id), update);
      });

      this.surfaces.forEach((surface, id) => {
        const update = updatesById.get(id) || {};
        const nodeState = this.resolveNode(surface);
        const deflectionDeg = Number(update.deflectionDeg);
        const currentDeflectionDeg = Number.isFinite(deflectionDeg) ? deflectionDeg : 0.0;
        const deflected = Math.abs(currentDeflectionDeg) > 1.0e-6;
        const matrixChanged = !!(
          nodeState && this.matricesDiffer(nodeState.node.matrix, nodeState.originalMatrix)
        );
        const resolved = !!nodeState;
        const surfaceFailures = [];
        if (!resolved) surfaceFailures.push(`node '${surface.node}' was not resolved`);
        if (deflected && resolved && !matrixChanged) {
          surfaceFailures.push('deflected surface matrix is still neutral');
        }
        failures.push(...surfaceFailures.map((message) => `${id}: ${message}`));
        surfaces.push({
          id,
          node: String(surface.node || ''),
          resolvedPivotNode: resolved ? String(surface.node || '') : '',
          mappingStatus: resolved ? 'resolved' : 'unresolved',
          polarity: Number.isFinite(Number(update.polarity)) ? Number(update.polarity) : null,
          profilePolarity: Number.isFinite(Number(update.profilePolarity)) ? Number(update.profilePolarity) : null,
          resolved,
          deflectionDeg: currentDeflectionDeg,
          deflected,
          matrixChanged,
          failures: surfaceFailures,
        });
      });

      return {
        ok: failures.length === 0,
        profileLoaded: !!this.profile,
        modelLoaded: !!this.model,
        surfaceCount: surfaces.length,
        surfaces,
        failures,
      };
    }
  }

  window.VehicleModelController = VehicleModelController;
})();
