# Publishing the GitHub Wiki

These files are written as GitHub Wiki Markdown pages. The main repo reports wiki support enabled, but the special wiki git repository must exist before it can be pushed directly.

## When the wiki repo exists

From a clean working folder outside this repo:

```powershell
git clone https://github.com/MKme/xnode.wiki.git
Copy-Item C:\GitHub\xnode\wiki\*.md C:\GitHub\xnode.wiki\ -Force
Set-Location C:\GitHub\xnode.wiki
git add .
git commit -m "Create XNODE product and software wiki"
git push origin master
```

If the wiki repository uses `main` instead of `master`, push the branch GitHub created.

## If clone still says repository not found

Open the GitHub web UI for `MKme/xnode`, go to the Wiki tab, and create any first page. GitHub creates the backing `MKme/xnode.wiki.git` repository after that first page exists. Then replace the generated page with these source-controlled pages.

## Source of truth

Keep these files in `wiki/` as the source of truth, then republish them into the GitHub Wiki after edits. That keeps wiki changes reviewable in normal firmware commits.

