// Auto-generated header file - DO NOT EDIT MANUALLY
// Generated from Web/index.html, styles.css, and script.js
// Run ./generate_web_header.sh to regenerate

#ifndef WEB_INTERFACE_H
#define WEB_INTERFACE_H

#include <Arduino.h>

static const char INDEX_HTML[] PROGMEM = R"HTML(
<!doctype html>
<html>
<head>
  <meta charset="utf-8" />
  <title>Block Party</title>
  <meta name="viewport" content="width=device-width, initial-scale=1" />
  <style>
    body {
        font-family: system-ui, Arial, sans-serif;
        margin: 16px;
    }
    
    #status {
        padding: 6px 10px;
        display: inline-block;
        border-radius: 6px;
        background: #eef;
    }
    
    .badge {
        padding: 2px 6px;
        border-radius: 6px;
        font-size: 12px;
    }
    
    .ok {
        background: #c6f6d5;
    }
    
    .out {
        background: #fed7d7;
    }
    
    .disc {
        background: #e2e8f0;
    }
    
    table {
        border-collapse: collapse;
        width: 100%;
        margin-top: 12px;
    }
    
    th,
    td {
        border-bottom: 1px solid #eee;
        text-align: left;
        padding: 6px 8px;
    }
    
    .column-container {
        display: flex;
        flex-direction: column;
    }
    
    .row-container {
        display: flex;
        flex-direction: row;
        gap: 8px;
        align-items: center;
        flex-wrap: wrap;
    }
    
    input[type=number] {
        width: 90px;
    }
    
    button {
        padding: 4px 8px;
        border-radius: 6px;
        font-size: 12px;
        border: none;
        background: #e2e8f0;
        cursor: pointer;
        font-family: inherit;
    }
    
    button:hover {
        background: #cbd5e0;
    }
    
    button:active {
        background: #a0aec0;
    }
  </style>
</head>
<body>
  <h1>Block Party — Multiplayer Bop-It</h1>
  <div id="status">Connecting…</div>

  <div class="column-container" style="margin:12px 0;">
    <div class="row-container">
      <h3>Admin Controls:</h3>
      <button id="startBtn" onclick="startGame()">Start</button>
      <button id="pauseBtn" onclick="pauseGame()">Pause</button>
      <button id="resumeBtn" onclick="resumeGame()">Resume</button>
      <button id="resetBtn" onclick="resetGame()">Reset</button>
    </div>
    <div class="row-container">
      <h3>Game Settings:</h3>
      <label>Round0(ms) <input id="round0" type="number" value="2500"></label>
      <label>Decay(ms) <input id="decay" type="number" value="150"></label>
      <label>Min(ms)   <input id="minms" type="number" value="800"></label>
    </div>
  </div>

  <h3 id="phaseRound"></h3>
  <h3 id="Round"></h3>
  <h3 id="Command"></h3>

  <table id="table">
    <thead><tr><th>Player</th><th>Block</th><th>In Game</th><th>Score</th><th>Conn</th><th>Reported</th><th>Success</th><th>Rename</th></tr></thead>
    <tbody></tbody>
  </table>

  <script>
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
        const reportedBadge = `<span class="badge ${p.reported?'ok':'out'}">${p.reported?'YES':'NO'}</span>`;
        const successBadge = `<span class="badge ${p.successful?'ok':'out'}">${p.successful?'YES':'NO'}</span>`;
    
        const disabledAttr = isLobby ? '' : 'disabled';
        tr.innerHTML = `
          <td>${p.name}</td>
          <td>${p.blockId}</td>
          <td>${inBadge}</td>
          <td>${p.score}</td>
          <td>${cBadge}</td>
          <td>${reportedBadge}</td>
          <td>${successBadge}</td>
          <td>
            <input size="10" value="${p.name}" id="name-${p.blockId}" ${disabledAttr}>
            <button onclick="renameBlock('${p.blockId}', document.getElementById('name-${p.blockId}').value)" ${disabledAttr}>Save</button>
          </td>`;
        tb.appendChild(tr);
      }
    }
  </script>
</body>
</html>
)HTML";

#endif // WEB_INTERFACE_H
