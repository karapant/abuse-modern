"use strict";

const repository = "karapant/abuse-modern";
const releasePath = `https://github.com/${repository}/releases/`;
const releaseStatus = document.querySelector("#release-status");
const sizeFormat = new Intl.NumberFormat("en", { minimumFractionDigits: 1, maximumFractionDigits: 1 });
const dateFormat = new Intl.DateTimeFormat("en", { dateStyle: "long", timeZone: "UTC" });

async function showLatestRelease() {
    const response = await fetch(`https://api.github.com/repos/${repository}/releases/latest`, {
        headers: { Accept: "application/vnd.github+json" }
    });
    if (response.status === 404) {
        releaseStatus.textContent = "The first public release will appear on GitHub when it’s published.";
        return;
    }
    if (!response.ok) return;

    const release = await response.json();
    if (!release.html_url.startsWith(`${releasePath}tag/`)) return;

    const notes = document.querySelector("#release-notes");
    notes.href = release.html_url;
    notes.hidden = false;

    const published = new Date(release.published_at);
    const releaseDate = release.published_at && Number.isFinite(published.getTime())
        ? ` · Published ${dateFormat.format(published)}`
        : "";
    releaseStatus.textContent = `Latest release ${release.tag_name}${releaseDate}`;

    const assets = release.assets.filter(({ browser_download_url }) =>
        browser_download_url.startsWith(`${releasePath}download/`)
    );

    for (const row of document.querySelectorAll("[data-package]")) {
        const download = row.querySelector("[data-download]");
        download.href = release.html_url;

        const asset = assets.find(({ name }) =>
            name.startsWith("abuse-modern-") && name.endsWith(`-${row.dataset.package}`)
        );
        if (!asset) continue;

        download.href = asset.browser_download_url;
        download.textContent = "Download";
        download.setAttribute("aria-label", `Download Abuse Modern for ${row.dataset.platform}`);

        if (Number.isFinite(asset.size) && asset.size > 0) {
            const size = row.querySelector("[data-size]");
            size.textContent = ` · ${sizeFormat.format(asset.size / 1_000_000)} MB`;
            size.hidden = false;
        }

        const checksum = assets.find(({ name }) => name === `${asset.name}.sha256`);
        if (checksum) {
            const link = row.querySelector("[data-checksum]");
            link.href = checksum.browser_download_url;
            link.setAttribute("aria-label", `SHA-256 checksum for ${row.dataset.platform}`);
            link.hidden = false;
        }

        if (row.dataset.package === "windows-x86_64-setup.exe") {
            document.querySelector("#windows-build").hidden = true;
        }
    }
}

showLatestRelease().catch(() => {
    // The package links already point to GitHub Releases.
});
