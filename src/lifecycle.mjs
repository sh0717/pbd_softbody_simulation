// Attempt every release in dependency order. Preserve the first failure, including
// a constructor/frame error supplied by the caller, even if later releases fail.
export function runCleanup(actions, primaryError) {
  let failed = arguments.length > 1;
  let firstError = primaryError;

  for (const action of actions) {
    try {
      action();
    } catch (error) {
      if (!failed) firstError = error;

      failed = true;
    }
  }

  if (failed) throw firstError;
}
