const ws = new WebSocket(`ws://${window.location.host}/ws`);
const state = { phase:'LOBBY', round:0, players:[] };

ws.onopen = () => {
  document.getElementById('status').textContent = 'Connected';
  // Identify this client as a web interface
  ws.send(JSON.stringify({type: 'web-hello', clientType: 'web'}));
};

ws.onclose = () => {
  document.getElementById('status').textContent = 'Disconnected';
};

ws.onmessage = (ev) => {
  try {
    const msg = JSON.parse(ev.data);
    if (msg.type === 'state') {
      state.phase = msg.phase;
      state.round = msg.round;
      state.players = msg.players || [];
      render();
    }
  } catch(e) {}
};

function sendAdmin(payload) {
  ws.send(JSON.stringify(Object.assign({type:'admin'}, payload)));
}

function startGame() {
  const round0 = +document.getElementById('round0').value || 2500;
  const decay  = +document.getElementById('decay').value || 150;
  const minms  = +document.getElementById('minms').value || 800;
  sendAdmin({action:'start', round0Ms:round0, decayMs:decay, minMs:minms});
}

function pauseGame(){ 
  sendAdmin({action:'pause'}); 
}

function resumeGame(){ 
  sendAdmin({action:'resume'}); 
}

function resetGame(){ 
  sendAdmin({action:'reset'}); 
}

function renameBlock(bid, name){
  sendAdmin({action:'rename', blockId:bid, name});
}

function render() {
  document.getElementById('phaseRound').textContent = `Phase: ${state.phase}`;
  document.getElementById('Round').textContent = `Round: ${state.round > 0 ? state.round : '-'}`;

  // Enable/disable controls based on game phase
  const isLobby = state.phase === 'LOBBY';
  document.getElementById('startBtn').disabled = !isLobby;
  document.getElementById('round0').disabled = !isLobby;
  document.getElementById('decay').disabled = !isLobby;
  document.getElementById('minms').disabled = !isLobby;

  const tb = document.querySelector('#table tbody');
  tb.innerHTML = '';
  const sorted = [...state.players].sort((a,b)=> b.score - a.score);
  for (const p of sorted) {
    const tr = document.createElement('tr');
    const inBadge = `<span class="badge ${p.inGame?'ok':'out'}">${p.inGame?'IN':'OUT'}</span>`;
    const cBadge  = `<span class="badge ${p.connected?'ok':'disc'}">${p.connected?'ON':'OFF'}</span>`;

    const disabledAttr = isLobby ? '' : 'disabled';
    tr.innerHTML = `
      <td>${p.name}</td>
      <td>${p.blockId}</td>
      <td>${inBadge}</td>
      <td>${p.score}</td>
      <td>${cBadge}</td>
      <td>
        <input size="10" value="${p.name}" id="name-${p.blockId}" ${disabledAttr}>
        <button onclick="renameBlock('${p.blockId}', document.getElementById('name-${p.blockId}').value)" ${disabledAttr}>Save</button>
      </td>`;
    tb.appendChild(tr);
  }
}
