import { useEffect, useRef } from 'react';

import { formatBytes } from '../format';

type Props = {
  bytes: number;
  onConfirm(): void;
  onCancel(): void;
};

// The only modal in the editing design: one confirmation before an undoable-not cleanup.
export function CleanupConfirm({ bytes, onConfirm, onCancel }: Props) {
  const cancel = useRef<HTMLButtonElement>(null);

  useEffect(() => {
    cancel.current?.focus();
    const onKey = (e: KeyboardEvent) => {
      if (e.key === 'Escape') onCancel();
    };
    window.addEventListener('keydown', onKey);
    return () => window.removeEventListener('keydown', onKey);
  }, [onCancel]);

  return (
    <div className="scrim">
      <div className="dlg cleanup-dlg" role="dialog" aria-modal="true" aria-label="Clean up disk">
        <div className="dlg-h">Free {formatBytes(bytes)}?</div>
        <div className="pane">
          <p>This cannot be undone.</p>
        </div>
        <div className="dlg-f">
          <button className="btn" ref={cancel} onClick={onCancel}>
            Cancel
          </button>
          <button className="btn danger" onClick={onConfirm}>
            Clean up
          </button>
        </div>
      </div>
    </div>
  );
}