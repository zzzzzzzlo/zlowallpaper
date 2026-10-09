const crypto = require('node:crypto');
const net = require('node:net');
const { EventEmitter } = require('node:events');

const PROTOCOL_VERSION = 1;

function managedArguments(argv = process.argv) {
  const values = new Map();
  for (const argument of argv) {
    const match = /^--([^=]+)=(.*)$/.exec(argument);
    if (match) values.set(match[1], match[2]);
  }

  return {
    enabled: argv.includes('--lightwallpaper-managed'),
    pipe: values.get('host-pipe') || '',
    token: values.get('host-token') || '',
    protocolVersion: Number(values.get('host-protocol-version')),
  };
}

function pipePath(name) {
  return `\\\\.\\pipe\\${name}`;
}

function tokensMatch(expected, received) {
  const left = Buffer.from(String(expected), 'utf8');
  const right = Buffer.from(String(received), 'utf8');
  return left.length === right.length && crypto.timingSafeEqual(left, right);
}

class HostIpc extends EventEmitter {
  constructor(options = managedArguments()) {
    super();
    this.options = options;
    this.socket = null;
    this.buffer = '';
    this.connected = false;
    this.stopping = false;
  }

  get enabled() {
    return this.options.enabled;
  }

  connect() {
    if (!this.enabled || this.socket || !this.options.pipe || !this.options.token) {
      return;
    }
    if (this.options.protocolVersion !== PROTOCOL_VERSION) {
      this.emit('warning', 'Icontra host protocol version is not supported.');
      return;
    }

    this.socket = net.createConnection({ path: pipePath(this.options.pipe) });
    this.socket.setEncoding('utf8');
    this.socket.on('connect', () => {
      this.connected = true;
      this.send('hello', {
        token: this.options.token,
        processId: process.pid,
      });
      this.emit('connected');
    });
    this.socket.on('data', (chunk) => this.consume(chunk));
    this.socket.on('error', (error) => this.emit('warning', `Host IPC error: ${error.message}`));
    this.socket.on('close', () => {
      const disconnected = this.connected;
      this.socket = null;
      this.connected = false;
      this.buffer = '';
      this.emit('disconnected', disconnected);
    });
  }

  consume(chunk) {
    this.buffer += chunk;
    const lines = this.buffer.split(/\r?\n/);
    this.buffer = lines.pop() || '';
    for (const line of lines) {
      if (!line.trim()) continue;
      let message;
      try {
        message = JSON.parse(line);
      } catch {
        this.send('warning', { message: 'Ignored malformed host IPC JSON.' });
        continue;
      }
      if (!message || typeof message.type !== 'string') continue;
      if (message.protocolVersion !== PROTOCOL_VERSION) {
        this.send('warning', { message: 'Ignored incompatible host IPC protocol.' });
        continue;
      }
      this.emit('message', message);
    }
  }

  send(type, payload = {}, requestId = '') {
    if (!this.socket || this.socket.destroyed) return false;
    const message = {
      type,
      requestId,
      payload,
      protocolVersion: PROTOCOL_VERSION,
    };
    return this.socket.write(`${JSON.stringify(message)}\n`);
  }

  close() {
    this.stopping = true;
    if (this.socket && !this.socket.destroyed) this.socket.end();
  }
}

module.exports = { HostIpc, PROTOCOL_VERSION, managedArguments, tokensMatch };
