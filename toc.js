// Populate the sidebar
//
// This is a script, and not included directly in the page, to control the total size of the book.
// The TOC contains an entry for each page, so if each page includes a copy of the TOC,
// the total size of the page becomes O(n**2).
class MDBookSidebarScrollbox extends HTMLElement {
    constructor() {
        super();
    }
    connectedCallback() {
        this.innerHTML = '<ol class="chapter"><li class="chapter-item expanded "><a href="foreword.html"><strong aria-hidden="true">1.</strong> Foreword</a></li><li class="chapter-item expanded affix "><li class="part-title">Getting started</li><li class="chapter-item expanded "><a href="compiling.html"><strong aria-hidden="true">2.</strong> Compiling</a></li><li><ol class="section"><li class="chapter-item expanded "><a href="header-only.html"><strong aria-hidden="true">2.1.</strong> Header-only support</a></li><li class="chapter-item expanded "><a href="cmake.html"><strong aria-hidden="true">2.2.</strong> Adding to a project</a></li></ol></li><li class="chapter-item expanded "><a href="enumerating.html"><strong aria-hidden="true">3.</strong> Enumerating ports</a></li><li class="chapter-item expanded "><a href="midi-1-in.html"><strong aria-hidden="true">4.</strong> MIDI 1 in</a></li><li class="chapter-item expanded "><a href="midi-1-out.html"><strong aria-hidden="true">5.</strong> MIDI 1 out</a></li><li class="chapter-item expanded "><a href="midi-2-in.html"><strong aria-hidden="true">6.</strong> MIDI 2 in</a></li><li class="chapter-item expanded "><a href="midi-2-out.html"><strong aria-hidden="true">7.</strong> MIDI 2 out</a></li><li class="chapter-item expanded "><a href="file.html"><strong aria-hidden="true">8.</strong> MIDI file support</a></li><li class="chapter-item expanded affix "><li class="part-title">Advanced features</li><li class="chapter-item expanded "><a href="midi-2-integrations.html"><strong aria-hidden="true">9.</strong> MIDI 2 integrations</a></li><li class="chapter-item expanded "><a href="hotplug.html"><strong aria-hidden="true">10.</strong> Hotplug support</a></li><li class="chapter-item expanded "><a href="error-handling.html"><strong aria-hidden="true">11.</strong> Error handling</a></li><li class="chapter-item expanded "><a href="configuration.html"><strong aria-hidden="true">12.</strong> Custom configuration</a></li><li><ol class="section"><li class="chapter-item expanded "><a href="context-sharing.html"><strong aria-hidden="true">12.1.</strong> Context sharing</a></li><li class="chapter-item expanded "><a href="polling.html"><strong aria-hidden="true">12.2.</strong> External polling</a></li><li class="chapter-item expanded "><a href="timestamping.html"><strong aria-hidden="true">12.3.</strong> Timestamping</a></li></ol></li><li class="chapter-item expanded "><a href="queue.html"><strong aria-hidden="true">13.</strong> Queue vs callbacks</a></li><li class="chapter-item expanded "><a href="keyboard.html"><strong aria-hidden="true">14.</strong> Computer keyboard input</a></li><li class="chapter-item expanded affix "><li class="part-title">Reference</li><li class="chapter-item expanded "><a href="backends.html"><strong aria-hidden="true">15.</strong> Backends</a></li></ol>';
        // Set the current, active page, and reveal it if it's hidden
        let current_page = document.location.href.toString().split("#")[0];
        if (current_page.endsWith("/")) {
            current_page += "index.html";
        }
        var links = Array.prototype.slice.call(this.querySelectorAll("a"));
        var l = links.length;
        for (var i = 0; i < l; ++i) {
            var link = links[i];
            var href = link.getAttribute("href");
            if (href && !href.startsWith("#") && !/^(?:[a-z+]+:)?\/\//.test(href)) {
                link.href = path_to_root + href;
            }
            // The "index" page is supposed to alias the first chapter in the book.
            if (link.href === current_page || (i === 0 && path_to_root === "" && current_page.endsWith("/index.html"))) {
                link.classList.add("active");
                var parent = link.parentElement;
                if (parent && parent.classList.contains("chapter-item")) {
                    parent.classList.add("expanded");
                }
                while (parent) {
                    if (parent.tagName === "LI" && parent.previousElementSibling) {
                        if (parent.previousElementSibling.classList.contains("chapter-item")) {
                            parent.previousElementSibling.classList.add("expanded");
                        }
                    }
                    parent = parent.parentElement;
                }
            }
        }
        // Track and set sidebar scroll position
        this.addEventListener('click', function(e) {
            if (e.target.tagName === 'A') {
                sessionStorage.setItem('sidebar-scroll', this.scrollTop);
            }
        }, { passive: true });
        var sidebarScrollTop = sessionStorage.getItem('sidebar-scroll');
        sessionStorage.removeItem('sidebar-scroll');
        if (sidebarScrollTop) {
            // preserve sidebar scroll position when navigating via links within sidebar
            this.scrollTop = sidebarScrollTop;
        } else {
            // scroll sidebar to current active section when navigating via "next/previous chapter" buttons
            var activeSection = document.querySelector('#sidebar .active');
            if (activeSection) {
                activeSection.scrollIntoView({ block: 'center' });
            }
        }
        // Toggle buttons
        var sidebarAnchorToggles = document.querySelectorAll('#sidebar a.toggle');
        function toggleSection(ev) {
            ev.currentTarget.parentElement.classList.toggle('expanded');
        }
        Array.from(sidebarAnchorToggles).forEach(function (el) {
            el.addEventListener('click', toggleSection);
        });
    }
}
window.customElements.define("mdbook-sidebar-scrollbox", MDBookSidebarScrollbox);
