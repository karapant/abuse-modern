"use strict";

const repository = "karapant/abuse-modern";
const releasePath = `https://github.com/${repository}/releases/`;
const status = document.querySelector("#release-status");

async function showLatestRelease() {
    const response = await fetch(`https://api.github.com/repos/${repository}/releases/latest`, {
        headers: { Accept: "application/vnd.github+json" }
    });
    if (response.status === 404) {
        status.textContent = "The first public release will appear on GitHub when it’s published.";
        return;
    }
    if (!response.ok) return;

    const release = await response.json();
    let hasPackages = false;
    for (const button of document.querySelectorAll("[data-package]")) {
        if (release.html_url.startsWith(`${releasePath}tag/`)) {
            button.href = release.html_url;
        }
        const asset = release.assets.find(({ name, browser_download_url }) =>
            name.startsWith("abuse-modern-") &&
            name.endsWith(`-${button.dataset.package}`) &&
            browser_download_url.startsWith(`${releasePath}download/`)
        );
        if (asset) {
            button.href = asset.browser_download_url;
            hasPackages = true;
        }
    }
    status.textContent = hasPackages
        ? `Latest release ${release.tag_name}. Choose a platform to download or view its release notes.`
        : `Latest release ${release.tag_name}. Open the release notes to see available files.`;
}

showLatestRelease().catch(() => {
    // The links already point to the GitHub Releases page.
});
