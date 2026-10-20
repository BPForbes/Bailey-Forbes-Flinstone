#!/usr/bin/env bash
# Put one validated browser lab into its slot on the deployment branch.
#
#   publish_pages_slot.sh <main|develop> <lab-dir> <site-dir>
#
# Run from a checkout of this repository that can push (actions/checkout's
# credentials). The deployment branch (PAGES_BRANCH, default "publisher") holds
# built labs only, never source:
#   /          the main lab    (written only by a main build)
#   /develop/  the develop lab (written only by a develop build)
# This replaces the given slot with <lab-dir> and keeps the other slot exactly
# as the branch held it. The result is left in <site-dir> for the Pages upload
# and force-pushed as one parentless commit, so built images never pile up in
# history. Callers run one publish at a time (one concurrency group); the push
# is also guarded with --force-with-lease against the commit this run read.
#
# Writes "deploy=true" to $GITHUB_OUTPUT (when set) only if the site has a main
# lab at its root: a develop publish before the first main publish updates the
# branch but must not deploy a site with nothing at /.
set -euo pipefail

slot="${1:?slot: main or develop}"
lab="${2:?lab directory}"
site="${3:?site directory}"
branch="${PAGES_BRANCH:-publisher}"

case "$slot" in
    main | develop) ;;
    *) echo "publish_pages_slot: unknown slot '$slot'" >&2; exit 2 ;;
esac
test -f "$lab/index.html" || { echo "publish_pages_slot: $lab has no index.html" >&2; exit 1; }
test ! -e "$site" || { echo "publish_pages_slot: $site already exists" >&2; exit 1; }
mkdir -p "$site"

lease="$branch:"   # empty lease: the branch must not exist yet
tracking="refs/remotes/origin/$branch"
if git fetch -q --depth=1 origin "+refs/heads/$branch:$tracking" 2>/dev/null; then
    lease="$branch:$(git rev-parse "$tracking")"
    git archive "$tracking" | tar -x -C "$site"
fi

if [ "$slot" = main ]; then
    # Everything at the root except the develop slot belongs to main.
    find "$site" -mindepth 1 -maxdepth 1 ! -name develop -exec rm -rf {} +
    cp -a "$lab/." "$site/"
else
    rm -rf "$site/develop"
    mkdir "$site/develop"
    cp -a "$lab/." "$site/develop/"
fi
touch "$site/.nojekyll"

gitdir="$(git rev-parse --absolute-git-dir)"
index="$(mktemp)"
rm -f "$index"
GIT_INDEX_FILE="$index" git -C "$site" --git-dir="$gitdir" --work-tree=. add -A -f .
tree="$(GIT_INDEX_FILE="$index" git write-tree)"
rm -f "$index"
commit="$(
    GIT_AUTHOR_NAME="github-actions[bot]" \
    GIT_AUTHOR_EMAIL="41898282+github-actions[bot]@users.noreply.github.com" \
    GIT_COMMITTER_NAME="github-actions[bot]" \
    GIT_COMMITTER_EMAIL="41898282+github-actions[bot]@users.noreply.github.com" \
    git commit-tree "$tree" -m "Publish $slot lab from ${GITHUB_SHA:-$(git rev-parse HEAD)}"
)"
git push -q --force-with-lease="$lease" origin "$commit:refs/heads/$branch"

deploy=false
test -f "$site/index.html" && deploy=true
echo "publish_pages_slot: $slot slot published to $branch as $commit; main lab at root: $deploy"
if [ -n "${GITHUB_OUTPUT:-}" ]; then
    echo "deploy=$deploy" >>"$GITHUB_OUTPUT"
fi
