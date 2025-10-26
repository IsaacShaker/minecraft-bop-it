const ws = new WebSocket(`ws://${window.location.host}/ws`);
const state = { phase:'LOBBY', round:0, currentCmd:'', players:[] };

// Cache for selected voice
let selectedVoice = null;

// Initialize voice selection
function initializeVoice() {
  if ('speechSynthesis' in window) {
    const voices = speechSynthesis.getVoices();
    
    // try to get any US English voice
    const englishVoice = voices.find(v => v.lang.includes('en-US'));
    if (englishVoice) {
      selectedVoice = englishVoice;
    }
  }
}

// Initialize voice when voices are available
if ('speechSynthesis' in window) {
  if (speechSynthesis.getVoices().length > 0) {
    initializeVoice();
  } else {
    speechSynthesis.onvoiceschanged = () => {
      initializeVoice();
    };
  }
}

// Speech synthesis function
function speakCommand(command) {
  if ('speechSynthesis' in window && command) {
    // Cancel any ongoing speech
    speechSynthesis.cancel();
    
    const utterance = new SpeechSynthesisUtterance(command);
    
    // Use cached voice if available
    if (selectedVoice) {
      utterance.voice = selectedVoice;
    }
    
    // Enhanced speech settings
    utterance.rate = 1.0;      // Normal speed
    utterance.pitch = 1.1;     // Slightly higher pitch for clarity
    utterance.volume = 0.9;    // Louder for better audibility
    
    speechSynthesis.speak(utterance);
  }
}

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
      const oldRound = state.round;
      state.phase = msg.phase || 'LOBBY';
      state.round = msg.round || 0;
      state.currentCmd = msg.currentCmd || '';
      state.players = msg.players || [];
      
      // Speak command if it's a new round with a command
      if (state.currentCmd && state.round > 0 && state.round !== oldRound) {
        speakCommand(state.currentCmd);
      }
      
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
  document.getElementById('Command').textContent = `Command: ${state.round > 0 ? state.currentCmd : '-'}`;

  // Enable/disable controls based on game phase
  const isLobby = state.phase === 'LOBBY';
  const isDone = state.phase === 'DONE';
  const canPauseResume = !isLobby && !isDone;
  
  document.getElementById('startBtn').disabled = !isLobby;
  document.getElementById('pauseBtn').disabled = !canPauseResume;
  document.getElementById('resumeBtn').disabled = !canPauseResume;
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
