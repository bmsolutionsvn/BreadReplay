import { useEffect, useState } from 'react';
import type { ReactNode } from 'react';

import { onShellMessage, postToShell } from './nativeVideoRegistry';

// The shell removed the system caption (WM_NCCALCSIZE); app.css makes this bar a drag region.
function Glyph({ children }: { children: ReactNode }) {
  return (
    <svg width="10" height="10" viewBox="0 0 12 12" fill="none" stroke="currentColor" aria-hidden="true">
      {children}
    </svg>
  );
}

export function TitleBar() {
  const [maximized, setMaximized] = useState(false);
  useEffect(() => onShellMessage((m) => m.type === 'windowState' && setMaximized(!!m.maximized)), []);
  const win = (action: string) => postToShell({ type: 'window', action });
  return (
    // preventDefault on mousedown keeps keyboard focus off the bar, so hotkeys still land.
    <header className="titlebar" onMouseDown={(e) => e.preventDefault()}>
      <span className="name">BreadReplay</span>
      <button aria-label="Minimise" onClick={() => win('minimize')}>
        <Glyph>
          <path d="M2 10h8" />
        </Glyph>
      </button>
      <button aria-label={maximized ? 'Restore' : 'Maximise'} onClick={() => win('maximize')}>
        <Glyph>
          {maximized ? (
            <>
              <rect x="3.5" y="1.5" width="7" height="7" />
              <path d="M1.5 3.5v7h7" />
            </>
          ) : (
            <rect x="2.5" y="2.5" width="7" height="7" />
          )}
        </Glyph>
      </button>
      <button className="close" aria-label="Quit BreadReplay" onClick={() => win('close')}>
        <Glyph>
          <path d="M2.5 2.5l7 7M9.5 2.5l-7 7" />
        </Glyph>
      </button>
    </header>
  );
}