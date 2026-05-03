#include "pages_dashboard.h"

// Return the dashboard HTML content.
std::string getDashboardPage() {
    return R"HTML(<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>Aaron File Directory</title>

<style>
    * {
        margin: 0;
        padding: 0;
        box-sizing: border-box;
    }

    body {
        font-family: "Segoe UI", Tahoma, Geneva, Verdana, sans-serif;
        background: #f3f6fa;
        color: #222;
        min-height: 100vh;
        display: flex;
        flex-direction: column;
    }

    .topbar {
        height: 60px;
        background: #1e293b;
        color: white;
        display: flex;
        align-items: center;
        justify-content: space-between;
        padding: 0 24px;
        font-size: 20px;
        font-weight: 600;
        box-shadow: 0 2px 10px rgba(0,0,0,0.15);
    }

    .topbar span {
        color: #38bdf8;
        margin-right: 10px;
    }

    .topbar button {
        border: none;
        background: #e2e8f0;
        color: #1e293b;
        border-radius: 14px;
        padding: 10px 16px;
        cursor: pointer;
        transition: background 0.2s ease;
    }

    .topbar button:hover {
        background: #cbd5e1;
    }

    .container {
        display: flex;
        flex: 1;
        padding: 24px;
        gap: 20px;
    }

    .sidebar {
        width: 240px;
        background: white;
        border-radius: 20px;
        border: 1px solid #e2e8f0;
        padding: 24px;
        box-shadow: 0 10px 20px rgba(0,0,0,0.05);
    }

    .sidebar h3 {
        font-size: 14px;
        text-transform: uppercase;
        color: #666;
        margin-bottom: 15px;
    }

    .sidebar ul {
        list-style: none;
    }

    .sidebar li {
        padding: 12px 14px;
        border-radius: 12px;
        cursor: pointer;
        margin-bottom: 8px;
        transition: 0.2s;
        color: #334155;
    }

    .sidebar li:hover,
    .sidebar li.active {
        background: #e8f0fe;
        color: #1a73e8;
        font-weight: 600;
    }

    .content {
        flex: 1;
        background: white;
        border-radius: 20px;
        padding: 28px;
        box-shadow: 0 10px 20px rgba(0,0,0,0.05);
    }

    .breadcrumb {
        font-size: 14px;
        color: #666;
        margin-bottom: 20px;
    }

    .section-header {
        display: flex;
        align-items: center;
        justify-content: space-between;
        margin-bottom: 20px;
    }

    .section-header h2 {
        font-size: 24px;
        color: #111827;
    }

    .section-header p {
        font-size: 14px;
        color: #475569;
    }

    .files-grid {
        display: grid;
        grid-template-columns: repeat(auto-fill, minmax(180px, 1fr));
        gap: 18px;
    }

    .file-card {
        background: #f8fafc;
        border-radius: 16px;
        padding: 18px;
        text-align: center;
        box-shadow: 0 8px 16px rgba(15,23,42,0.05);
        transition: transform 0.2s ease, box-shadow 0.2s ease;
        display: flex;
        flex-direction: column;
        align-items: center;
        justify-content: center;
    }

    .file-card:hover {
        transform: translateY(-4px);
        box-shadow: 0 14px 24px rgba(15,23,42,0.08);
    }

    .file-icon {
        font-size: 40px;
        margin-bottom: 14px;
    }

    .folder {
        color: #f59e0b;
    }

    .html {
        color: #1a73e8;
    }

    .txt {
        color: #10b981;
    }

    .file-name {
        font-size: 16px;
        font-weight: 700;
        margin-bottom: 6px;
        color: #0f172a;
    }

    .file-meta {
        font-size: 13px;
        color: #64748b;
    }

    .file-actions {
        display: flex;
        justify-content: center;
        gap: 10px;
        margin-top: 14px;
        width: 100%;
    }

    .action-button {
        padding: 8px 12px;
        border-radius: 999px;
        border: 1px solid #cbd5e1;
        background: white;
        color: #1e293b;
        font-size: 13px;
        text-decoration: none;
        transition: background 0.2s ease, color 0.2s ease, border-color 0.2s ease;
    }

    .action-button:hover {
        background: #e8f0fe;
        border-color: #a5b4fc;
        color: #1d4ed8;
    }

    .action-button.secondary {
        border-color: #c7d2fe;
    }

    .empty-state {
        color: #475569;
        font-size: 15px;
        padding: 20px;
        text-align: center;
        border: 1px dashed #cbd5e1;
        border-radius: 16px;
        background: #f8fafc;
    }

    .footer {
        background: white;
        border-top: 1px solid #e2e8f0;
        padding: 16px 24px;
        font-size: 13px;
        color: #64748b;
        border-radius: 0 0 20px 20px;
        margin: 0 24px 24px;
    }

    a.file-link {
        color: inherit;
        text-decoration: none;
        width: 100%;
    }
</style>
</head>

<body>

<div class="topbar">
    <div><span>📁</span> Aaron's File Explorer</div>
    <button id="logoutButton">Log out</button>
</div>

<div class="container">

    <div class="sidebar">
        <h3>Quick Access</h3>
        <ul>
            <li><a href="/dashboard" class="file-link">Home</a></li>
            <li><a href="/holiday" class="file-link">Holidays (API)</a></li>
            <li><a href="/dashboard" class="file-link">Extra &#128521;</a></li>
        </ul>
    </div>

    <div class="content">
        <div class="breadcrumb">
            Home / Web Server / Your Folder
        </div>

        <div class="section-header">
            <div>
                <h2>Your personal folder</h2>
                <p id="folderName">Loading your folder...</p>
            </div>
            <div id="fileCount" style="color:#475569; font-size:14px;">&nbsp;</div>
        </div>

        <div class="files-grid" id="filesGrid"></div>
    </div>

</div>

<div class="footer">
    This page shows only the contents of your own folder.
</div>

<script>
    function createFileCard(name) {
        const isFolder = name.endsWith('/');
        const icon = isFolder ? '📂' : name.endsWith('.html') ? '🌐' : '📄';
        const typeClass = isFolder ? 'folder' : name.endsWith('.html') ? 'html' : 'txt';
        const displayName = isFolder ? name.slice(0, -1) : name;

        const card = document.createElement('div');
        card.className = 'file-card';

        const title = document.createElement('div');
        title.innerHTML = `
            <div class="file-icon ${typeClass}">${icon}</div>
            <div class="file-name">${displayName}</div>
            <div class="file-meta">${isFolder ? 'Folder' : 'File'}</div>
        `;
        card.appendChild(title);

        if (!isFolder) {
            const actions = document.createElement('div');
            actions.className = 'file-actions';

            const viewLink = document.createElement('a');
            viewLink.className = 'action-button';
            viewLink.href = '/files/' + encodeURIComponent(name);
            viewLink.target = '_blank';
            viewLink.textContent = 'View';

            const downloadLink = document.createElement('a');
            downloadLink.className = 'action-button secondary';
            downloadLink.href = '/download/' + encodeURIComponent(name);
            downloadLink.textContent = 'Download';

            actions.appendChild(viewLink);
            actions.appendChild(downloadLink);
            card.appendChild(actions);
        }

        return card;
    }

    async function loadSession() {
        try {
            const response = await fetch('/session');
            if (!response.ok) {
                window.location.href = '/';
                return;
            }
            const data = await response.json();
            document.getElementById('folderName').textContent = `Folder: ${data.folderName}`;
            document.getElementById('fileCount').textContent = `${data.entries.length} item${data.entries.length === 1 ? '' : 's'}`;

            const filesGrid = document.getElementById('filesGrid');
            filesGrid.innerHTML = '';

            if (data.entries.length === 0) {
                const empty = document.createElement('div');
                empty.className = 'empty-state';
                empty.textContent = 'Your folder is empty. Refresh after adding files or using your server.';
                filesGrid.appendChild(empty);
                return;
            }

            data.entries.forEach(name => {
                filesGrid.appendChild(createFileCard(name));
            });
        } catch (error) {
            const filesGrid = document.getElementById('filesGrid');
            filesGrid.innerHTML = '<div class="empty-state">Could not load your folder. Refresh the page or try again later.</div>';
            console.error(error);
        }
    }

    document.getElementById('logoutButton').addEventListener('click', () => {
        window.location.href = '/logout';
    });

    loadSession();
</script>

</body>
</html>)HTML";
}
