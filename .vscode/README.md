# VS Code GitHub Multiple Account Configuration

This directory contains VS Code workspace settings that enable support for multiple GitHub accounts.

## Features

- Multiple GitHub account authentication
- Easy switching between GitHub accounts
- Support for GitHub Pull Requests and Issues extension
- Configured queries for assigned PRs, created PRs, and review requests

## How to Use Multiple GitHub Accounts in VS Code

### Step 1: Sign in with Your First GitHub Account

1. Open VS Code
2. Press `Ctrl+Shift+P` (Windows/Linux) or `Cmd+Shift+P` (macOS) to open the Command Palette
3. Type `GitHub: Sign in` and select it
4. Follow the authentication flow in your browser
5. Authorize VS Code to access your GitHub account

### Step 2: Add a Second GitHub Account

1. Press `Ctrl+Shift+P` (Windows/Linux) or `Cmd+Shift+P` (macOS) again
2. Type `GitHub: Sign in` and select it
3. In the authentication page, you may need to:
   - Sign out from your current GitHub session in the browser
   - Or use a different browser profile
   - Or use an incognito/private window
4. Sign in with your second GitHub account
5. Authorize VS Code to access this account as well

### Step 3: Switch Between Accounts

Once you have multiple accounts added:

1. Look for the GitHub icon in the VS Code status bar (bottom)
2. Click on your current account name
3. A menu will appear showing all your signed-in accounts
4. Select the account you want to use for the current operation

## Recommended Extensions

To get the most out of multiple GitHub accounts, install these VS Code extensions:

- **GitHub Pull Requests and Issues** (GitHub.vscode-pull-request-github)
  - Manage PRs and issues directly from VS Code
  - Already configured in settings.json
  
- **GitHub Repositories** (GitHub.remotehub)
  - Browse and edit GitHub repositories directly in VS Code

## Account-Specific Git Configuration

If you need different Git configurations (name, email) for different accounts:

### Option 1: Per-Repository Configuration

```bash
# In your repository directory
git config user.name "Your Name"
git config user.email "your.email@example.com"
```

### Option 2: Conditional Git Configuration

Add to your `~/.gitconfig`:

```ini
[includeIf "gitdir:~/work/personal/"]
    path = ~/.gitconfig-personal

[includeIf "gitdir:~/work/company/"]
    path = ~/.gitconfig-company
```

Then create separate config files:

**~/.gitconfig-personal:**
```ini
[user]
    name = Your Personal Name
    email = personal@example.com
```

**~/.gitconfig-company:**
```ini
[user]
    name = Your Work Name
    email = work@company.com
```

## Troubleshooting

### Issue: Can't add second account

**Solution:** Try these steps:
1. Sign out from all GitHub accounts in your browser
2. Clear VS Code authentication: Run `GitHub: Sign out` from Command Palette for all accounts
3. Start fresh by signing in again

### Issue: Wrong account is being used for operations

**Solution:**
1. Check the status bar to see which account is currently active
2. Click on the account name to switch
3. Ensure your git config matches the desired account

### Issue: Authentication token expired

**Solution:**
1. Open Command Palette (`Ctrl+Shift+P` / `Cmd+Shift+P`)
2. Run `GitHub: Sign out`
3. Run `GitHub: Sign in` again to refresh the token

## Additional Resources

- [GitHub Authentication in VS Code](https://code.visualstudio.com/docs/editor/github)
- [VS Code GitHub Pull Requests Extension](https://marketplace.visualstudio.com/items?itemName=GitHub.vscode-pull-request-github)
- [Working with GitHub in VS Code](https://code.visualstudio.com/docs/sourcecontrol/github)
