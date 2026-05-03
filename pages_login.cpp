#include "pages_login.h"

// Return the login HTML page as a single string. Use a raw string literal
// so the HTML is easy to read in-source.
std::string getLoginPage() {
    return R"HTML(<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Login - Aaron File Directory</title>
    <style>
        * {
            margin: 0;
            padding: 0;
            box-sizing: border-box;
        }
        body {
            font-family: "Segoe UI", Tahoma, Geneva, Verdana, sans-serif;
            background: #eef2ff;
            color: #111827;
            min-height: 100vh;
            display: flex;
            justify-content: center;
            align-items: center;
            padding: 24px;
        }
        .login-card {
            width: min(520px, 100%);
            background: white;
            border-radius: 24px;
            box-shadow: 0 25px 60px rgba(15, 23, 42, 0.12);
            padding: 36px;
        }
        .logo {
            font-size: 28px;
            font-weight: 700;
            color: #1e293b;
            margin-bottom: 20px;
        }
        .subtitle {
            color: #475569;
            margin-bottom: 32px;
            line-height: 1.6;
        }
        .field {
            margin-bottom: 20px;
        }
        label {
            display: block;
            margin-bottom: 10px;
            font-size: 14px;
            color: #475569;
        }
        input {
            width: 100%;
            border: 1px solid #e2e8f0;
            border-radius: 14px;
            padding: 14px 16px;
            font-size: 15px;
            color: #0f172a;
            background: #f8fafc;
        }
        input:focus {
            outline: none;
            border-color: #6366f1;
            background: white;
        }
        button {
            width: 100%;
            padding: 14px 16px;
            border-radius: 14px;
            border: none;
            background: #4f46e5;
            color: white;
            font-size: 15px;
            font-weight: 700;
            cursor: pointer;
            box-shadow: 0 12px 30px rgba(79, 70, 229, 0.18);
        }
        button:hover {
            background: #4338ca;
        }
        .help-text {
            margin-top: 18px;
            font-size: 13px;
            color: #64748b;
            text-align: center;
        }
        .error {
            margin-bottom: 20px;
            padding: 14px 16px;
            border-radius: 14px;
            background: #fee2e2;
            color: #b91c1c;
            border: 1px solid #fecaca;
        }
    </style>
</head>
<body>
    <div class="login-card">
        <div class="logo">Aaron's File Directory</div>
        <div class="subtitle">Enter the username and password provided by Aaron to access your private folder.</div>
        <div id="errorMessage" class="error" style="display:none;"></div>
        <form method="POST" action="/login">
            <div class="field">
                <label for="username">Username</label>
                <input id="username" name="username" type="text" autocomplete="username" required>
            </div>
            <div class="field">
                <label for="password">Password</label>
                <input id="password" name="password" type="password" autocomplete="current-password" required>
            </div>
            <button type="submit">Sign In</button>
        </form>
        <div class="help-text">Only preconfigured accounts can sign in. Contact Aaron for credentials.</div>
        <div class="help-text">Email: aaronarseneau@yahoo.com<br>
            <a href="https://discord.com/users/160185559866671105">Discord</a>
        </div>
    </div>
    <script>
        const params = new URLSearchParams(window.location.search);
        if (params.get('error') === 'invalid') {
            const errorElement = document.getElementById('errorMessage');
            errorElement.textContent = 'Invalid username or password. Please try again.';
            errorElement.style.display = 'block';
        }
    </script>
</body>
</html>)HTML";
}
