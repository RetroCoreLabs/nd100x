//
// SPDX-License-Identifier: MIT
// Copyright (c) 1985-2026 Ronny Hansen
// RetroCore Labs — https://github.com/HackerCorpLabs
// Emulating yesterday's technology with today's code
//

// help-window.js - SINTRAN help glass window, search, navigation

let helpContentLoaded = false;
let helpSections = [];
let currentSearchTerm = '';
let currentSection = 'all';
let searchMatches = [];
let currentMatchIndex = -1;

// Close help window handlers
document.getElementById('help-window-close').addEventListener('click', function() {
  closeHelpWindow();
});

// Search functionality - event delegation
document.addEventListener('keydown', function(e) {
  if (e.target && e.target.id === 'help-search' && e.key === 'Enter') {
    e.preventDefault();
    handleHelpSearch();
  }
});

document.addEventListener('click', function(e) {
  if (e.target && e.target.id === 'help-search-clear') {
    clearHelpSearch();
  } else if (e.target && e.target.classList.contains('help-nav-btn')) {
    handleSectionNav(e.target);
  } else if (e.target && e.target.id === 'search-prev') {
    navigateToMatch(currentMatchIndex - 1);
  } else if (e.target && e.target.id === 'search-next') {
    navigateToMatch(currentMatchIndex + 1);
  }
});

function openHelpWindow() {
  document.getElementById('help-window').style.display = 'flex';

  if (!helpContentLoaded) {
    loadHelpContent();
  } else {
    document.getElementById('help-content').style.display = 'block';
    document.getElementById('help-navigation').style.display = 'block';
  }
}

function closeHelpWindow() {
  document.getElementById('help-window').style.display = 'none';
}

async function loadHelpContent() {
  const helpLoading = document.getElementById('help-loading');
  const helpContent = document.getElementById('help-content');

  helpLoading.style.display = 'flex';
  helpContent.style.display = 'none';

  try {
    console.log('Loading SINTRAN commands reference...');
    const response = await fetch('SINTRAN-Commands.html');
    if (!response.ok) {
      throw new Error(`Failed to load help content: ${response.status}`);
    }

    const htmlContent = await response.text();

    const parser = new DOMParser();
    const doc = parser.parseFromString(htmlContent, 'text/html');
    const bodyContent = doc.body.innerHTML;

    helpContent.innerHTML = bodyContent;

    const objectElements = helpContent.querySelectorAll('object');
    objectElements.forEach(obj => {
      if (obj.innerHTML) {
        obj.outerHTML = obj.innerHTML;
      } else {
        obj.remove();
      }
    });

    const scripts = doc.querySelectorAll('script');
    scripts.forEach(script => {
      if (script.innerHTML) {
        try {
          eval(script.innerHTML);
        } catch (e) {
          console.warn('Could not execute help script:', e);
        }
      }
    });

    helpContentLoaded = true;
    helpLoading.style.display = 'none';
    helpContent.style.display = 'block';

    document.getElementById('help-navigation').style.display = 'block';

    initializeHelpFunctionality();

    console.log('SINTRAN commands reference loaded successfully');

  } catch (error) {
    console.error('Error loading help content:', error);
    helpContent.innerHTML = `
      <div style="text-align: center; color: #e53935; padding: 40px;">
        <h2>Error Loading Help Content</h2>
        <p>${error.message}</p>
        <p><small>Please check that SINTRAN-Commands.html is available in the same directory.</small></p>
      </div>
    `;
    helpLoading.style.display = 'none';
    helpContent.style.display = 'block';
  }
}

function initializeHelpFunctionality() {
  const helpContent = document.getElementById('help-content');

  const navDiv = helpContent.querySelector('.nav');
  if (navDiv) navDiv.remove();

  const introPara = helpContent.querySelector('p');
  if (introPara && introPara.textContent.includes('This document contains a reference of all available SINTRAN III commands')) {
    introPara.remove();
  }

  const sectionDivs = helpContent.querySelectorAll('div.section');
  helpSections = [];

  const indexH2 = helpContent.querySelector('h2#index');
  if (indexH2) {
    let indexContent = [indexH2];
    let nextElement = indexH2.nextElementSibling;

    while (nextElement && !nextElement.classList.contains('section')) {
      indexContent.push(nextElement);
      nextElement = nextElement.nextElementSibling;
    }

    helpSections.push({
      id: 'index',
      title: 'Index',
      elements: indexContent
    });
  }

  sectionDivs.forEach((sectionDiv, index) => {
    const sectionHeaderDiv = sectionDiv.querySelector('.section-header');
    const h2Element = sectionHeaderDiv ? sectionHeaderDiv.querySelector('h2[id]') : null;

    if (h2Element) {
      const sectionId = h2Element.id;
      const sectionTitle = h2Element.textContent.trim();

      let sectionElements = [];
      sectionElements.push(sectionDiv);

      const allChildElements = Array.from(sectionDiv.querySelectorAll('*'));
      allChildElements.forEach(child => {
        if (sectionDiv.contains(child) && child.closest('.section') === sectionDiv) {
          sectionElements.push(child);
        }
      });

      helpSections.push({
        id: sectionId,
        title: sectionTitle,
        elements: sectionElements
      });
    }
  });
}

function handleHelpSearch() {
  const searchInput = document.getElementById('help-search');
  const searchNavigation = document.getElementById('search-navigation');
  const clearBtn = document.getElementById('help-search-clear');
  const searchTerm = searchInput.value.toLowerCase().trim();

  currentSearchTerm = searchTerm;

  searchNavigation.style.display = searchTerm ? 'flex' : 'none';
  clearBtn.style.display = searchTerm ? 'block' : 'none';

  searchMatches = [];
  currentMatchIndex = -1;

  applyFilters();

  if (searchMatches.length > 0) {
    navigateToMatch(0);
  }
}

function clearHelpSearch() {
  const searchInput = document.getElementById('help-search');
  const searchNavigation = document.getElementById('search-navigation');
  const clearBtn = document.getElementById('help-search-clear');

  searchInput.value = '';
  currentSearchTerm = '';
  searchNavigation.style.display = 'none';
  clearBtn.style.display = 'none';

  searchMatches = [];
  currentMatchIndex = -1;

  removeNoSearchResults();
  clearSearchHighlights();
  applyFilters();
}

function handleSectionNav(button) {
  document.querySelectorAll('.help-nav-btn').forEach(btn => {
    btn.classList.remove('active');
  });
  button.classList.add('active');

  currentSection = button.dataset.section;
  applyFilters();

  if (currentSection !== 'all') {
    const helpContent = document.getElementById('help-content');
    helpContent.scrollTop = 0;
    setTimeout(() => {
      scrollToSection(currentSection);
    }, 200);
  }
}

function applyFilters() {
  const helpContent = document.getElementById('help-content');

  removeNoSearchResults();
  clearSearchHighlights();

  helpSections.forEach(section => {
    section.elements.forEach(el => {
      el.classList.remove('section-hidden');
      el.style.display = '';
    });
  });

  if (currentSection !== 'all') {
    helpSections.forEach(section => {
      if (section.id !== currentSection) {
        section.elements.forEach(el => {
          el.classList.add('section-hidden');
        });
      } else {
        section.elements.forEach(el => {
          el.classList.remove('section-hidden');
          let parent = el.parentElement;
          while (parent && parent !== helpContent) {
            if (parent.classList.contains('section-hidden')) {
              parent.classList.remove('section-hidden');
            }
            parent = parent.parentElement;
          }
        });
      }
    });
  }

  if (currentSearchTerm) {
    const searchRegex = new RegExp(`(${escapeRegex(currentSearchTerm)})`, 'gi');
    let hasVisibleResults = false;

    helpSections.forEach(section => {
      if (currentSection === 'all' || section.id === currentSection) {
        let sectionHasMatches = false;

        section.elements.forEach(el => {
          const textContent = el.textContent.toLowerCase();
          if (textContent.includes(currentSearchTerm)) {
            sectionHasMatches = true;
            hasVisibleResults = true;
            highlightSearchTerm(el, searchRegex);
          }
        });

        if (!sectionHasMatches && currentSearchTerm) {
          section.elements.forEach(el => {
            el.classList.add('section-hidden');
          });
        }
      }
    });

    if (!hasVisibleResults) {
      showNoSearchResults();
    }
  }

  updateSearchUI();
}

function highlightSearchTerm(element, regex) {
  const walker = document.createTreeWalker(element, NodeFilter.SHOW_TEXT, null, false);

  const textNodes = [];
  let node;
  while (node = walker.nextNode()) {
    textNodes.push(node);
  }

  textNodes.forEach(textNode => {
    if (regex.test(textNode.textContent)) {
      const span = document.createElement('span');
      span.innerHTML = textNode.textContent.replace(regex, '<span class="search-highlight">$1</span>');
      textNode.parentNode.replaceChild(span, textNode);

      const highlights = span.querySelectorAll('.search-highlight');
      highlights.forEach(highlight => {
        searchMatches.push(highlight);
      });
    }
  });
}

function clearSearchHighlights() {
  const highlights = document.querySelectorAll('.help-content .search-highlight');
  highlights.forEach(highlight => {
    const parent = highlight.parentNode;
    parent.replaceChild(document.createTextNode(highlight.textContent), highlight);
    parent.normalize();
  });
}

function scrollToSection(sectionId) {
  const helpContent = document.getElementById('help-content');
  const targetElement = helpContent.querySelector(`#${sectionId}`);

  if (targetElement) {
    targetElement.scrollIntoView({
      behavior: 'smooth',
      block: 'start'
    });
  }
}

function showNoSearchResults() {
  const helpContent = document.getElementById('help-content');
  const existing = helpContent.querySelector('.no-search-results');

  if (!existing) {
    const noResults = document.createElement('div');
    noResults.className = 'no-search-results';
    noResults.style.cssText = 'text-align: center; padding: 40px; color: #666; font-style: italic;';
    noResults.innerHTML = `
      <p>No results found for "${escapeHtml(currentSearchTerm)}"</p>
      <p><small>Try adjusting your search terms or selecting a different section.</small></p>
    `;
    helpContent.appendChild(noResults);
  }
}

function removeNoSearchResults() {
  const helpContent = document.getElementById('help-content');
  const existing = helpContent.querySelector('.no-search-results');
  if (existing) existing.remove();
}

function escapeRegex(string) {
  return string.replace(/[.*+?^${}()|[\]\\]/g, '\\$&');
}

function navigateToMatch(matchIndex) {
  if (searchMatches.length === 0) return;

  if (matchIndex < 0) matchIndex = 0;
  if (matchIndex >= searchMatches.length) matchIndex = searchMatches.length - 1;

  searchMatches.forEach(match => {
    match.classList.remove('current-match');
  });

  currentMatchIndex = matchIndex;

  const currentMatch = searchMatches[currentMatchIndex];
  currentMatch.classList.add('current-match');

  scrollToMatch(currentMatch);
  updateSearchUI();
}

function scrollToMatch(matchElement) {
  const helpContent = document.getElementById('help-content');
  const contentHeight = helpContent.clientHeight;

  let parent = matchElement.offsetParent;
  let actualTop = matchElement.offsetTop;

  while (parent && parent !== helpContent) {
    actualTop += parent.offsetTop;
    parent = parent.offsetParent;
  }

  helpContent.scrollTo({
    top: actualTop - (contentHeight / 2),
    behavior: 'smooth'
  });
}

function updateSearchUI() {
  const resultsCount = document.getElementById('search-results-count');
  const prevBtn = document.getElementById('search-prev');
  const nextBtn = document.getElementById('search-next');

  if (searchMatches.length === 0) {
    resultsCount.textContent = 'No matches found';
  } else {
    resultsCount.textContent = `Match ${currentMatchIndex + 1} of ${searchMatches.length}`;
  }

  const hasMatches = searchMatches.length > 0;
  const isFirst = currentMatchIndex <= 0;
  const isLast = currentMatchIndex >= searchMatches.length - 1;

  if (prevBtn) prevBtn.disabled = !hasMatches || isFirst;
  if (nextBtn) nextBtn.disabled = !hasMatches || isLast;
}
