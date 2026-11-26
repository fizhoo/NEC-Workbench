#!/usr/bin/env bash
# Publishes the GitHub release for a tag with the files in a folder, safely re-runnable:
#   no release yet       -> create a draft, upload every file, publish
#   a draft for the tag  -> keep the files it has when they are byte-identical to ours,
#                           upload the missing ones, publish; any other file there stops it
#   a published release  -> stop, changing nothing
# The tag must point at the commit this run built (GITHUB_SHA).
# Usage: publish-release.sh <tag> <title-file> <notes-file> <folder>
# Needs gh with GH_TOKEN, GITHUB_REPOSITORY and GITHUB_SHA, run in a checkout of the repository.
set -euo pipefail
tag=$1 title=$2 notes=$3 folder=$4

tagged=$(git ls-remote origin "refs/tags/$tag^{}" | cut -f1)
[ -n "$tagged" ] || tagged=$(git ls-remote origin "refs/tags/$tag" | cut -f1)
if [ "$tagged" != "$GITHUB_SHA" ]; then
    echo "::error title=Release::tag $tag is at ${tagged:-nothing} on GitHub, not at the built commit $GITHUB_SHA"
    exit 1
fi

state=$(gh release view "$tag" --json isDraft --jq 'if .isDraft then "draft" else "published" end' 2>/dev/null \
    || echo none)
case "$state" in
    published)
        echo "::error title=Release::$tag is already published; it was not changed"
        exit 1 ;;
    none)
        prerelease=()
        case "$tag" in *-*) prerelease=(--prerelease) ;; esac
        gh release create "$tag" --verify-tag --draft "${prerelease[@]}" \
            --title "$(cat "$title")" --notes-file "$notes"
        echo "created draft $tag" ;;
    draft)
        echo "resuming draft $tag" ;;
esac

ours=$(cd "$folder" && ls)
present=$(gh release view "$tag" --json assets --jq '.assets[].name')
check=$(mktemp -d)
for name in $present; do
    if ! grep -qxF "$name" <<< "$ours"; then
        echo "::error title=Release::draft $tag has $name, which this run does not attach; delete it from the draft and re-run"
        exit 1
    fi
    gh release download "$tag" --pattern "$name" --dir "$check"
    if ! cmp -s "$check/$name" "$folder/$name"; then
        echo "::error title=Release::draft $tag has a different $name than this run built; delete it from the draft and re-run"
        exit 1
    fi
    echo "kept $name"
done
for name in $ours; do
    grep -qxF "$name" <<< "$present" && continue
    gh release upload "$tag" "$folder/$name"
    echo "uploaded $name"
done
gh release edit "$tag" --draft=false
echo "published $tag"
