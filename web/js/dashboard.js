// ===========================================================================
// Sentinel AntiCheat — Admin Dashboard JavaScript
// ===========================================================================
// Handles all frontend logic: data fetching, UI rendering, real-time updates,
// chart management, and user interactions.
// ===========================================================================

// ── API Configuration ───────────────────────────────────────────────────
const API = {
    baseUrl: 'http://' + window.location.hostname + ':8080',
    token: 'change-me-sentinel-admin-token',  // Should be configured
    endpoints: {
        dashboard:   '/api/v1/dashboard',
        players:     '/api/v1/players',
        detections:  '/api/v1/detections',
        risk:        '/api/v1/risk/leaderboard',
        bans:        '/api/v1/bans',
        logs:        '/api/v1/logs',
        statistics:  '/api/v1/statistics',
        health:      '/api/v1/health'
    }
};

// ── State ───────────────────────────────────────────────────────────────
const state = {
    players: [],
    detections: [],
    riskData: [],
    bans: [],
    logs: [],
    stats: null,
    refreshInterval: null,
    charts: {}
};

// ── Navigation ──────────────────────────────────────────────────────────
document.querySelectorAll('.nav-item').forEach(item => {
    item.addEventListener('click', function() {
        const view = this.dataset.view;
        document.querySelectorAll('.nav-item').forEach(n => n.classList.remove('active'));
        this.classList.add('active');
        document.querySelectorAll('.view').forEach(v => v.classList.remove('active'));
        document.getElementById('view-' + view).classList.add('active');

        // Refresh data for the selected view
        switch(view) {
            case 'players': fetchPlayers(); break;
            case 'detections': fetchDetections(); break;
            case 'risk': fetchRiskLeaderboard(); break;
            case 'bans': fetchBans(); break;
            case 'logs': fetchLogs(); break;
            case 'statistics': fetchStatistics(); break;
        }
    });
});

// ── Data Fetching ───────────────────────────────────────────────────────

async function apiFetch(endpoint, method = 'GET', body = null) {
    const headers = {
        'Authorization': 'Bearer ' + API.token,
        'Content-Type': 'application/json'
    };
    try {
        const response = await fetch(API.baseUrl + endpoint, { method, headers, body: body ? JSON.stringify(body) : null });
        if (!response.ok) throw new Error('HTTP ' + response.status);
        return await response.json();
    } catch (err) {
        console.error('API Error:', err);
        return null;
    }
}

async function refreshData() {
    fetchDashboard();
}

async function fetchDashboard() {
    const data = await apiFetch(API.endpoints.dashboard);
    if (!data) return;

    state.stats = data;

    document.getElementById('statPlayers').textContent = data.players_tracked || 0;
    document.getElementById('statDetections').textContent = data.total_detections || 0;
    document.getElementById('statBans').textContent = data.active_bans || 0;
    document.getElementById('statSuspicious').textContent = data.suspicious_players || 0;
    document.getElementById('statDetections24h').textContent = data.detections_24h || 0;
    document.getElementById('statUptime').textContent = (data.uptime_hours || 0) + 'h';

    document.getElementById('playerCount').textContent = data.players_tracked || 0;

    renderRecentDetections(data.recent_detections || []);
    renderRiskChart(data.risk_distribution || {});
}

async function fetchPlayers() {
    const data = await apiFetch(API.endpoints.players);
    if (!data) return;
    state.players = data.players || [];
    renderPlayers();
}

async function fetchDetections() {
    const data = await apiFetch(API.endpoints.detections);
    if (!data) return;
    state.detections = data.detections || [];
    renderDetections();
}

async function fetchRiskLeaderboard() {
    const data = await apiFetch(API.endpoints.risk);
    if (!data) return;
    state.riskData = data.risk_data || [];
    renderRiskLeaderboard();
}

async function fetchBans() {
    const data = await apiFetch(API.endpoints.bans);
    if (!data) return;
    state.bans = data.bans || [];
    renderBans();
}

async function fetchLogs() {
    const data = await apiFetch(API.endpoints.logs);
    if (!data) return;
    state.logs = data.logs || [];
    renderLogs();
}

async function fetchStatistics() {
    const data = await apiFetch(API.endpoints.statistics);
    if (!data) return;
    state.stats = data;
    renderStatistics();
}

// ── Rendering ───────────────────────────────────────────────────────────

function renderRecentDetections(detections) {
    const container = document.getElementById('recentDetections');
    if (!detections.length) {
        container.innerHTML = '<div class="timeline-empty">No recent detections</div>';
        return;
    }

    container.innerHTML = detections.slice(0, 10).map(d => {
        let dotClass = 'behavior';
        const type = (d.type || '').toLowerCase();
        if (type.includes('aim') || type.includes('snap') || type.includes('silent')) dotClass = 'aim';
        else if (type.includes('bhop') || type.includes('speed') || type.includes('strafe')) dotClass = 'movement';
        else if (type.includes('lag') || type.includes('packet') || type.includes('flood')) dotClass = 'network';

        return `<div class="timeline-item">
            <div class="timeline-dot ${dotClass}"></div>
            <div class="timeline-content">
                <div class="timeline-title">${d.player_name || 'Unknown'} — ${d.type || 'Detection'}</div>
                <div class="timeline-desc">${d.evidence || d.description || ''} (Score: ${d.score || 0})</div>
                <div class="timeline-time">${d.time || ''}</div>
            </div>
        </div>`;
    }).join('');
}

function renderRiskChart(distribution) {
    const canvas = document.getElementById('riskChart');
    if (!canvas) return;
    if (state.charts.risk) state.charts.risk.destroy();

    const ctx = canvas.getContext('2d');
    state.charts.risk = new Chart(ctx, {
        type: 'doughnut',
        data: {
            labels: ['Safe', 'Suspicious', 'Highly Suspicious', 'Cheating', 'Banned'],
            datasets: [{
                data: [
                    distribution.safe || 0,
                    distribution.suspicious || 0,
                    distribution.highly_suspicious || 0,
                    distribution.cheating || 0,
                    distribution.banned || 0
                ],
                backgroundColor: ['#3fb950', '#d29922', '#f0883e', '#f85149', '#da3633'],
                borderColor: '#161b22',
                borderWidth: 2
            }]
        },
        options: {
            responsive: true,
            maintainAspectRatio: false,
            plugins: {
                legend: {
                    position: 'right',
                    labels: { color: '#8b949e', font: { family: 'Inter' } }
                }
            }
        }
    });
}

function renderPlayers() {
    const tbody = document.getElementById('playersBody');
    if (!state.players.length) {
        tbody.innerHTML = '<tr><td colspan="8" class="loading-row">No players connected</td></tr>';
        return;
    }

    tbody.innerHTML = state.players.map((p, i) => {
        const riskClass = getRiskClass(p.risk_score || 0);
        return `<tr>
            <td>${i + 1}</td>
            <td><strong>${escapeHtml(p.name || 'Unknown')}</strong></td>
            <td style="font-family:var(--font-mono);font-size:12px;">${escapeHtml(p.steam_id || 'N/A')}</td>
            <td style="font-family:var(--font-mono);font-size:12px;">${escapeHtml(p.ip || 'N/A')}</td>
            <td>${p.kills || 0}/${p.deaths || 0}</td>
            <td>${(p.headshot_ratio || 0).toFixed(1)}%</td>
            <td><span class="risk-badge ${riskClass}">${p.risk_score || 0}</span></td>
            <td>
                <div class="action-btns">
                    <button class="btn btn-small btn-secondary" onclick="viewPlayer('${escapeHtml(p.steam_id || p.ip || '')}')">View</button>
                    <button class="btn btn-small btn-danger" onclick="kickPlayer('${escapeHtml(p.steam_id || '')}')">Kick</button>
                </div>
            </td>
        </tr>`;
    }).join('');
}

function renderDetections() {
    const tbody = document.getElementById('detectionsBody');
    const filter = document.getElementById('detectionFilter')?.value || 'all';

    let filtered = state.detections;
    if (filter !== 'all') {
        filtered = filtered.filter(d => {
            const type = (d.type || '').toLowerCase();
            if (filter === 'aim') return type.includes('aim') || type.includes('snap') || type.includes('trigger');
            if (filter === 'movement') return type.includes('bhop') || type.includes('speed') || type.includes('strafe') || type.includes('duck');
            if (filter === 'network') return type.includes('lag') || type.includes('packet') || type.includes('flood');
            return true;
        });
    }

    if (!filtered.length) {
        tbody.innerHTML = '<tr><td colspan="6" class="loading-row">No detections</td></tr>';
        return;
    }

    tbody.innerHTML = filtered.slice(0, 50).map(d => {
        return `<tr>
            <td style="font-family:var(--font-mono);font-size:12px;">${d.time || ''}</td>
            <td><strong>${escapeHtml(d.player_name || 'Unknown')}</strong></td>
            <td><span class="risk-badge risk-cheating">${escapeHtml(d.type || 'Unknown')}</span></td>
            <td><strong>${d.score || 0}</strong></td>
            <td>${(d.confidence || 0).toFixed(2)}</td>
            <td style="max-width:300px;overflow:hidden;text-overflow:ellipsis;white-space:nowrap;">${escapeHtml(d.evidence || d.description || '')}</td>
        </tr>`;
    }).join('');
}

function renderRiskLeaderboard() {
    const tbody = document.getElementById('riskBody');
    if (!state.riskData.length) {
        tbody.innerHTML = '<tr><td colspan="10" class="loading-row">No risk data</td></tr>';
        return;
    }

    tbody.innerHTML = state.riskData.map((p, i) => {
        const riskClass = getRiskClass(p.total_score || 0);
        return `<tr>
            <td>${i + 1}</td>
            <td><strong>${escapeHtml(p.name || 'Unknown')}</strong></td>
            <td><strong>${p.total_score || 0}</strong></td>
            <td>${p.aim_score || 0}</td>
            <td>${p.movement_score || 0}</td>
            <td>${p.network_score || 0}</td>
            <td>${p.behavior_score || 0}</td>
            <td>${p.detection_count || 0}</td>
            <td><span class="risk-badge ${riskClass}">${getRiskLevel(p.total_score || 0)}</span></td>
            <td>
                <div class="action-btns">
                    <button class="btn btn-small btn-secondary" onclick="viewPlayer('${escapeHtml(p.player_key || '')}')">View</button>
                    <button class="btn btn-small btn-danger" onclick="banPlayer('${escapeHtml(p.player_key || '')}')">Ban</button>
                </div>
            </td>
        </tr>`;
    }).join('');
}

function renderBans() {
    const tbody = document.getElementById('bansBody');
    if (!state.bans.length) {
        tbody.innerHTML = '<tr><td colspan="8" class="loading-row">No active bans</td></tr>';
        return;
    }

    tbody.innerHTML = state.bans.map(b => {
        const status = b.active ? '<span style="color:var(--accent-red);font-weight:600;">Active</span>' : '<span style="color:var(--text-muted);">Expired</span>';
        return `<tr>
            <td><strong>${escapeHtml(b.name || 'Unknown')}</strong></td>
            <td style="font-family:var(--font-mono);font-size:12px;">${escapeHtml(b.player_key || '')}</td>
            <td>${b.ban_type === 5 ? 'Permanent' : 'Temporary'}</td>
            <td>${escapeHtml(b.reason || '')}</td>
            <td style="font-family:var(--font-mono);font-size:12px;">${b.issued_at || ''}</td>
            <td style="font-family:var(--font-mono);font-size:12px;">${b.expires_at || 'Never'}</td>
            <td>${status}</td>
            <td>
                <div class="action-btns">
                    ${b.active ? `<button class="btn btn-small btn-secondary" onclick="unbanPlayer('${escapeHtml(b.player_key || '')}')">Unban</button>` : ''}
                </div>
            </td>
        </tr>`;
    }).join('');
}

function renderLogs() {
    const container = document.getElementById('logContainer');
    const level = document.getElementById('logLevel')?.value || 'all';

    let filtered = state.logs;
    if (level !== 'all') {
        filtered = filtered.filter(l => (l.level || '').toLowerCase() === level);
    }

    if (!filtered.length) {
        container.innerHTML = '<div class="log-empty">No log entries</div>';
        return;
    }

    container.innerHTML = filtered.slice(0, 100).map(l => {
        return `<div class="log-entry">
            <span class="log-time">${l.time || ''}</span>
            <span class="log-level ${l.level || 'INFO'}">${l.level || 'INFO'}</span>
            <span class="log-module">${escapeHtml(l.module || '')}</span>
            <span class="log-msg">${escapeHtml(l.message || '')}</span>
        </div>`;
    }).join('');
}

function renderStatistics() {
    if (!state.stats) return;

    document.getElementById('metricResponse').textContent = (state.stats.avg_response_ms || 0).toFixed(1) + 'ms';
    document.getElementById('metricPeakPlayers').textContent = state.stats.peak_players || 0;
    document.getElementById('metricBanRate').textContent = (state.stats.ban_rate || 0).toFixed(1) + '%';
    document.getElementById('metricFPR').textContent = (state.stats.false_positive_rate || 0).toFixed(1) + '%';

    // Detection chart
    const detCanvas = document.getElementById('detectionChart');
    if (detCanvas && state.charts.detections) state.charts.detections.destroy();
    if (detCanvas) {
        const detCtx = detCanvas.getContext('2d');
        state.charts.detections = new Chart(detCtx, {
            type: 'line',
            data: {
                labels: (state.stats.detection_timeline || []).map(d => d.label),
                datasets: [{
                    label: 'Detections',
                    data: (state.stats.detection_timeline || []).map(d => d.count),
                    borderColor: '#58a6ff',
                    backgroundColor: 'rgba(88,166,255,0.1)',
                    fill: true,
                    tension: 0.4
                }]
            },
            options: {
                responsive: true,
                maintainAspectRatio: false,
                plugins: {
                    legend: { labels: { color: '#8b949e' } }
                },
                scales: {
                    x: { ticks: { color: '#6e7681' }, grid: { color: 'rgba(48,54,61,0.5)' } },
                    y: { ticks: { color: '#6e7681' }, grid: { color: 'rgba(48,54,61,0.5)' } }
                }
            }
        });
    }

    // Type chart
    const typeCanvas = document.getElementById('typeChart');
    if (typeCanvas && state.charts.types) state.charts.types.destroy();
    if (typeCanvas) {
        const typeCtx = typeCanvas.getContext('2d');
        state.charts.types = new Chart(typeCtx, {
            type: 'bar',
            data: {
                labels: ['Aim', 'Movement', 'Network', 'Behavior'],
                datasets: [{
                    label: 'Detections by Type',
                    data: [
                        state.stats.aim_detections || 0,
                        state.stats.movement_detections || 0,
                        state.stats.network_detections || 0,
                        state.stats.behavior_detections || 0
                    ],
                    backgroundColor: ['#f85149', '#d29922', '#a371f7', '#f0883e'],
                    borderRadius: 4
                }]
            },
            options: {
                responsive: true,
                maintainAspectRatio: false,
                plugins: { legend: { display: false } },
                scales: {
                    x: { ticks: { color: '#6e7681' }, grid: { display: false } },
                    y: { ticks: { color: '#6e7681' }, grid: { color: 'rgba(48,54,61,0.5)' } }
                }
            }
        });
    }
}

// ── Search & Filter ─────────────────────────────────────────────────────

function searchPlayers(query) {
    if (!query) {
        renderPlayers();
        return;
    }
    const q = query.toLowerCase();
    const filtered = state.players.filter(p =>
        (p.name || '').toLowerCase().includes(q) ||
        (p.steam_id || '').toLowerCase().includes(q) ||
        (p.ip || '').toLowerCase().includes(q)
    );
    const tbody = document.getElementById('playersBody');
    if (!filtered.length) {
        tbody.innerHTML = '<tr><td colspan="8" class="loading-row">No matching players</td></tr>';
        return;
    }
    // Re-render with filtered data
    const orig = state.players;
    state.players = filtered;
    renderPlayers();
    state.players = orig;
}

function filterDetections() {
    renderDetections();
}

function filterLogs() {
    renderLogs();
}

// ── Player Actions ──────────────────────────────────────────────────────

function viewPlayer(playerKey) {
    if (!playerKey) return;
    // Switch to a player detail view (simplified — shows in detections tab)
    document.querySelector('[data-view="detections"]').click();
    document.getElementById('playerSearch').value = playerKey;
    setTimeout(() => searchPlayers(playerKey), 100);
}

async function kickPlayer(playerKey) {
    if (!confirm('Kick player ' + playerKey + '?')) return;
    const result = await apiFetch('/api/v1/actions/kick', 'POST', { player_key: playerKey });
    if (result && result.success) {
        alert('Player kicked successfully');
        fetchPlayers();
    } else {
        alert('Failed to kick player');
    }
}

async function banPlayer(playerKey) {
    document.getElementById('banPlayerKey').value = playerKey;
    showBanModal();
}

async function unbanPlayer(playerKey) {
    if (!confirm('Unban ' + playerKey + '?')) return;
    const result = await apiFetch('/api/v1/bans/unban', 'POST', { player_key: playerKey });
    if (result && result.success) {
        alert('Player unbanned');
        fetchBans();
    } else {
        alert('Failed to unban player');
    }
}

// ── Ban Modal ───────────────────────────────────────────────────────────

function showBanModal() {
    document.getElementById('banModal').classList.add('active');
}

function hideBanModal() {
    document.getElementById('banModal').classList.remove('active');
}

async function issueBan() {
    const data = {
        player_key: document.getElementById('banPlayerKey').value,
        ban_type: parseInt(document.getElementById('banType').value),
        duration_minutes: parseInt(document.getElementById('banDuration').value),
        reason: document.getElementById('banReason').value
    };

    if (!data.player_key) {
        alert('Please enter a SteamID or IP');
        return;
    }

    const result = await apiFetch('/api/v1/bans', 'POST', data);
    if (result && result.success) {
        alert('Ban issued successfully');
        hideBanModal();
        fetchBans();
    } else {
        alert('Failed to issue ban');
    }
}

// ── Settings ────────────────────────────────────────────────────────────

async function saveSettings() {
    const settings = {
        modules: {},
        thresholds: {}
    };

    document.querySelectorAll('#moduleToggles input').forEach(toggle => {
        const name = toggle.closest('.toggle-item').querySelector('span').textContent;
        settings.modules[name] = toggle.checked;
    });

    const result = await apiFetch('/api/v1/config', 'POST', settings);
    if (result && result.success) {
        alert('Settings saved successfully');
    } else {
        alert('Failed to save settings');
    }
}

// ── Modal Close on Overlay Click ────────────────────────────────────────
document.getElementById('banModal').addEventListener('click', function(e) {
    if (e.target === this) hideBanModal();
});

// ── Utility Functions ───────────────────────────────────────────────────

function escapeHtml(str) {
    if (!str) return '';
    return str.replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;')
              .replace(/"/g, '&quot;').replace(/'/g, '&#039;');
}

function getRiskClass(score) {
    if (score >= 150) return 'risk-banned';
    if (score >= 100) return 'risk-cheating';
    if (score >= 70) return 'risk-highly';
    if (score >= 40) return 'risk-suspicious';
    return 'risk-safe';
}

function getRiskLevel(score) {
    if (score >= 150) return 'Banned';
    if (score >= 100) return 'Cheating';
    if (score >= 70) return 'Highly Suspicious';
    if (score >= 40) return 'Suspicious';
    return 'Safe';
}

// ── Initialize ──────────────────────────────────────────────────────────
document.addEventListener('DOMContentLoaded', function() {
    fetchDashboard();
    // Auto-refresh every 10 seconds
    state.refreshInterval = setInterval(fetchDashboard, 10000);
});
