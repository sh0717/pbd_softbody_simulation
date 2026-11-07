/* Compatibility names for the C++20 contact solver exposed by physics.js. */
(function (root) {
  'use strict';
  const api =
    typeof module === 'object' && module.exports ? require('./physics.js') : root.PBDPhysics;
  const contacts = {
    SoftContactSolver: api.SoftContactSolver,
    closestPointTriangle: api.closestPointTriangle,
  };
  if (typeof module === 'object' && module.exports) module.exports = contacts;
  else root.PBDSoftContacts = contacts;
})(typeof globalThis !== 'undefined' ? globalThis : this);
