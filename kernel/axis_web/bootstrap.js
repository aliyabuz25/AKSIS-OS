/* Browser API subset. Native node handles live for the whole document. */
(() => {
  const native = globalThis.__axisDOM, cache = new Map();
  delete globalThis.__axisDOM;
  class Event {
    constructor(type, options = {}) { this.type = String(type); this.bubbles = !!options.bubbles; this.cancelable = !!options.cancelable; this.defaultPrevented = false; this.target = null; this.currentTarget = null; this._stop = false; }
    preventDefault() { if (this.cancelable) this.defaultPrevented = true; }
    stopPropagation() { this._stop = true; }
  }
  class Node {
    constructor(id) { this._id = id; this._listeners = new Map(); }
    get nodeType() { return native('type', this._id); }
    get nodeName() { return native('tag', this._id).toUpperCase(); }
    get parentNode() { return wrap(native('parent', this._id)); }
    get childNodes() { return native('children', this._id).map(wrap); }
    get children() { return this.childNodes.filter(n => n.nodeType === 1); }
    get firstChild() { return this.childNodes[0] || null; }
    get lastChild() { return this.childNodes.at(-1) || null; }
    get nextSibling() { return wrap(native('next', this._id)); }
    get ownerDocument() { return document; }
    get textContent() { return native('text', this._id); }
    set textContent(value) { native('text', this._id, String(value ?? '')); }
    get nodeValue() { return this.nodeType === 3 ? this.textContent : null; }
    set nodeValue(value) { if (this.nodeType === 3) this.textContent = value; }
    appendChild(node) { native('append', this._id, node._id); return node; }
    insertBefore(node, before) { native('insert', this._id, node._id, before ? before._id : 0); return node; }
    removeChild(node) { native('remove', this._id, node._id); return node; }
    remove() { if (this.parentNode) this.parentNode.removeChild(this); }
    append(...nodes) { for (const n of nodes) this.appendChild(n instanceof Node ? n : document.createTextNode(String(n))); }
    replaceChildren(...nodes) { this.textContent = ''; this.append(...nodes); }
    addEventListener(type, fn, options = {}) {
      if (!fn) return;
      const entries = this._listeners.get(type) || [];
      if (!entries.some(e => e.fn === fn)) entries.push({fn, once: !!options.once});
      this._listeners.set(type, entries);
    }
    removeEventListener(type, fn) { this._listeners.set(type, (this._listeners.get(type) || []).filter(e => e.fn !== fn)); }
    dispatchEvent(event) {
      if (!(event instanceof Event)) throw new TypeError('Expected Event');
      event.target = this;
      for (let node = this; node; node = event.bubbles && !event._stop ? node.parentNode : null) {
        event.currentTarget = node;
        for (const entry of [...(node._listeners.get(event.type) || [])]) {
          if (entry.once) node.removeEventListener(event.type, entry.fn);
          if (typeof entry.fn === 'function') entry.fn.call(node, event);
          else entry.fn.handleEvent(event);
        }
        if (typeof node['on' + event.type] === 'function') node['on' + event.type](event);
      }
      event.currentTarget = null;
      return !event.defaultPrevented;
    }
    querySelectorAll(selector) { return native('query', this._id, String(selector)).map(wrap); }
    querySelector(selector) { return this.querySelectorAll(selector)[0] || null; }
  }
  class Element extends Node {
    get tagName() { return this.nodeName; }
    getAttribute(name) { return native('getAttr', this._id, String(name)); }
    setAttribute(name, value) { native('setAttr', this._id, String(name), String(value)); }
    removeAttribute(name) { native('removeAttr', this._id, String(name)); }
    hasAttribute(name) { return this.getAttribute(name) !== null; }
    get id() { return this.getAttribute('id') || ''; }
    set id(v) { this.setAttribute('id', v); }
    get className() { return this.getAttribute('class') || ''; }
    set className(v) { this.setAttribute('class', v); }
    get classList() {
      const el = this;
      return {
        contains(c) { return el.className.split(/\s+/).includes(c); },
        add(...cs) { el.className = [...new Set([...el.className.split(/\s+/).filter(Boolean), ...cs])].join(' '); },
        remove(...cs) { el.className = el.className.split(/\s+/).filter(c => !cs.includes(c)).join(' '); },
        toggle(c, force) { const yes = force === undefined ? !this.contains(c) : !!force; yes ? this.add(c) : this.remove(c); return yes; }
      };
    }
    get style() {
      const el = this, cssName = p => p.replace(/[A-Z]/g, c => '-' + c.toLowerCase());
      return new Proxy({
        setProperty(k, v) { const map = new Map((el.getAttribute('style') || '').split(';').filter(Boolean).map(s => { const i = s.indexOf(':'); return [s.slice(0, i).trim(), s.slice(i + 1).trim()]; })); map.set(k, String(v)); el.setAttribute('style', [...map].map(([a,b]) => a + ':' + b).join(';')); },
        getPropertyValue(k) { return native('style', el._id, k); }
      }, {get(t,p) { return p === 'cssText' ? el.getAttribute('style') || '' : p in t ? t[p] : t.getPropertyValue(cssName(p)); }, set(t,p,v) { p === 'cssText' ? el.setAttribute('style', v) : t.setProperty(cssName(p), v); return true; }});
    }
    set innerHTML(v) { native('html', this._id, String(v)); }
    get innerHTML() { return native('html', this._id); }
    get hidden() { return this.hasAttribute('hidden'); }
    set hidden(v) { v ? this.setAttribute('hidden', '') : this.removeAttribute('hidden'); }
    click() { this.dispatchEvent(new Event('click', {bubbles: true, cancelable: true})); }
  }
  function wrap(id) { if (!id) return null; if (!cache.has(id)) cache.set(id, native('type', id) === 1 ? new Element(id) : new Node(id)); return cache.get(id); }
  const document = wrap(native('document', 0));
  document.createElement = tag => wrap(native('create', 0, String(tag)));
  document.createTextNode = text => wrap(native('createText', 0, String(text)));
  document.createDocumentFragment = () => wrap(native('fragment', 0));
  document.getElementById = id => wrap(native('getId', 0, String(id)));
  Object.defineProperties(document, {body: {get: () => wrap(native('body', 0))}, head: {get: () => wrap(native('head', 0))}, documentElement: {get: () => document.children[0]}, title: {get: () => native('title', 0), set: v => native('title', 0, String(v))}});
  document.readyState = 'loading';
  Object.assign(globalThis, {window: globalThis, self: globalThis, document, Node, Element, HTMLElement: Element, Event});
  globalThis.addEventListener = document.addEventListener.bind(document);
  globalThis.removeEventListener = document.removeEventListener.bind(document);
  globalThis.dispatchEvent = document.dispatchEvent.bind(document);
  const timer = globalThis.__axisTimer; delete globalThis.__axisTimer;
  globalThis.setTimeout = (fn, ms = 0, ...args) => { if (typeof fn !== 'function') throw new TypeError('Timer requires a function'); return timer(() => fn(...args), Number(ms), false); };
  globalThis.setInterval = (fn, ms = 0, ...args) => { if (typeof fn !== 'function') throw new TypeError('Timer requires a function'); return timer(() => fn(...args), Number(ms), true); };
  globalThis.clearTimeout = globalThis.clearInterval = id => timer(null, Number(id), false);
  globalThis.requestAnimationFrame = fn => setTimeout(() => fn(performance.now()), 16);
  globalThis.cancelAnimationFrame = clearTimeout;
  globalThis.queueMicrotask = fn => Promise.resolve().then(fn);
  globalThis.__axisClick = id => !wrap(id).dispatchEvent(new Event('click', {bubbles:true,cancelable:true}));
  globalThis.__axisReady = () => { document.readyState='interactive'; document.dispatchEvent(new Event('DOMContentLoaded')); document.readyState='complete'; document.dispatchEvent(new Event('load')); };
})();
