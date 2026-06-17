#!/bin/bash
#
# Generate index.html for the published package tree (the gh-pages checkout
# passed as $1, default ".") by listing every .rpm / .deb found under it.
# Re-run after adding packages so the landing page stays in sync.
#
set -eu
root="${1:-.}"
cd "${root}"

{
    cat <<'HEAD'
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>mod_rpaf-fork packages</title>
<style>
  body { font: 16px/1.6 system-ui, sans-serif; max-width: 760px; margin: 3rem auto; padding: 0 1rem; }
  code { background: #f4f4f4; padding: .1em .3em; border-radius: 4px; }
  li { margin: .2em 0; word-break: break-all; }
</style>
</head>
<body>
<h1>mod_rpaf-fork packages</h1>
<p>Prebuilt packages for
  <a href="https://github.com/y-ken/mod_rpaf">y-ken/mod_rpaf</a>,
  installable by URL (e.g. <code>yum localinstall &lt;url&gt;</code>).</p>
<ul>
HEAD
    find . -type f \( -name '*.rpm' -o -name '*.deb' \) \
        | sed 's|^\./||' | sort | while read -r f; do
        printf '<li><a href="%s">%s</a></li>\n' "$f" "$f"
    done
    cat <<'FOOT'
</ul>
</body>
</html>
FOOT
} > index.html
