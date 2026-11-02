import {
  createContext,
  useCallback,
  useContext,
  useEffect,
  useLayoutEffect,
  useRef,
  useState,
  type MouseEvent as ReactMouseEvent,
  type ReactNode,
} from 'react';

export type MenuItem =
  | { label: string; onSelect: () => void; hint?: string; danger?: boolean; disabled?: boolean }
  | 'separator';

type OpenMenu = { x: number; y: number; items: MenuItem[] };
type Show = (e: ReactMouseEvent, items: MenuItem[]) => void;

const MenuContext = createContext<Show>(() => {});

// Right-click handler for any element: onContextMenu={(e) => menu(e, items)}.
export const useContextMenu = () => useContext(MenuContext);

function editable(target: EventTarget | null): boolean {
  const el = target as HTMLElement | null;
  return !!el && (el.isContentEditable || ['INPUT', 'TEXTAREA', 'SELECT'].includes(el.tagName));
}

// One menu for the whole app. The browser's own menu (Back, Reload, Inspect...) never shows;
// text fields keep theirs for cut/copy/paste.
export function ContextMenuHost({ children }: { children: ReactNode }) {
  const [open, setOpen] = useState<OpenMenu | null>(null);
  const [active, setActiveState] = useState(-1);
  const activeRef = useRef(-1);
  const setActive = (i: number) => {
    activeRef.current = i;
    setActiveState(i);
  };
  const box = useRef<HTMLDivElement>(null);
  const close = useCallback(() => setOpen(null), []);

  const show = useCallback<Show>((e, items) => {
    e.preventDefault();
    e.stopPropagation();
    if (items.length === 0) return;
    setActive(-1);
    setOpen({ x: e.clientX, y: e.clientY, items });
  }, []);

  useEffect(() => {
    const block = (e: MouseEvent) => {
      if (!editable(e.target)) e.preventDefault();
    };
    window.addEventListener('contextmenu', block);
    return () => window.removeEventListener('contextmenu', block);
  }, []);

  useEffect(() => {
    if (!open) return;
    const selectable = open.items.map((it, i) => (it !== 'separator' && !it.disabled ? i : -1)).filter((i) => i >= 0);
    const onDown = (e: PointerEvent) => {
      if (!box.current?.contains(e.target as Node)) close();
    };
    // Capture phase: while the menu is open no key reaches the operator hotkeys (Enter = Take).
    const onKey = (e: KeyboardEvent) => {
      e.stopPropagation();
      e.preventDefault();
      if (e.key === 'Escape') close();
      else if (e.key === 'ArrowDown' || e.key === 'ArrowUp') {
        const at = selectable.indexOf(activeRef.current);
        const step = e.key === 'ArrowDown' ? 1 : -1;
        const next = at < 0 ? (step > 0 ? 0 : selectable.length - 1) : (at + step + selectable.length) % selectable.length;
        setActive(selectable[next] ?? -1);
      } else if (e.key === 'Enter' || e.key === ' ') {
        const it = open.items[activeRef.current];
        if (it && it !== 'separator' && !it.disabled) {
          close();
          it.onSelect();
        }
      }
    };
    window.addEventListener('pointerdown', onDown, true);
    window.addEventListener('keydown', onKey, true);
    window.addEventListener('blur', close);
    window.addEventListener('resize', close);
    return () => {
      window.removeEventListener('pointerdown', onDown, true);
      window.removeEventListener('keydown', onKey, true);
      window.removeEventListener('blur', close);
      window.removeEventListener('resize', close);
    };
  }, [open, close]);

  // Keep the menu inside the window.
  useLayoutEffect(() => {
    const el = box.current;
    if (!open || !el) return;
    const r = el.getBoundingClientRect();
    el.style.left = `${Math.max(4, Math.min(open.x, window.innerWidth - r.width - 4))}px`;
    el.style.top = `${Math.max(4, Math.min(open.y, window.innerHeight - r.height - 4))}px`;
  }, [open]);

  return (
    <MenuContext.Provider value={show}>
      {children}
      {open && (
        <div ref={box} className="ctx" role="menu" style={{ left: open.x, top: open.y }} onContextMenu={(e) => e.preventDefault()}>
          {open.items.map((it, i) =>
            it === 'separator' ? (
              <hr key={i} />
            ) : (
              <button
                key={i}
                role="menuitem"
                className={`${it.danger ? 'danger' : ''} ${active === i ? 'on' : ''}`}
                disabled={it.disabled}
                onMouseEnter={() => setActive(i)}
                onClick={() => {
                  close();
                  it.onSelect();
                }}
              >
                <span>{it.label}</span>
                {it.hint && <kbd>{it.hint}</kbd>}
              </button>
            ),
          )}
        </div>
      )}
    </MenuContext.Provider>
  );
}
