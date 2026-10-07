//
// SPDX-License-Identifier: MIT
// Copyright (c) 1985-2026 Ronny Hansen
// RetroCore Labs — https://github.com/HackerCorpLabs
// Emulating yesterday's technology with today's code
//

// theme-manager.js - Theme load/save/apply logic
// Manages body[data-theme] attribute and localStorage persistence

(function() {
  'use strict';

  var STORAGE_KEY = 'nd100x-theme';
  var DEFAULT_THEME = 'dark';
  var VALID_THEMES = ['dark', 'light', 'amber', 'nord', 'synthwave'];

  // Apply theme immediately (before paint if possible)
  function applyTheme(name) {
    if (VALID_THEMES.indexOf(name) === -1) name = DEFAULT_THEME;
    if (name === DEFAULT_THEME) {
      document.body.removeAttribute('data-theme');
    } else {
      document.body.setAttribute('data-theme', name);
    }
  }

  function getTheme() {
    try {
      var saved = localStorage.getItem(STORAGE_KEY);
      if (saved && VALID_THEMES.indexOf(saved) !== -1) return saved;
    } catch(e) {}
    return DEFAULT_THEME;
  }

  function setTheme(name) {
    if (VALID_THEMES.indexOf(name) === -1) name = DEFAULT_THEME;
    applyTheme(name);
    try {
      localStorage.setItem(STORAGE_KEY, name);
    } catch(e) {}
    // Update config window active state if open
    updateConfigCards(name);
    // Broadcast to pop-out terminal windows
    if (typeof window.broadcastThemeChange === 'function') {
      window.broadcastThemeChange(name);
    }
  }

  function updateConfigCards(name) {
    var cards = document.querySelectorAll('.theme-card');
    for (var i = 0; i < cards.length; i++) {
      if (cards[i].getAttribute('data-theme') === name) {
        cards[i].classList.add('active');
      } else {
        cards[i].classList.remove('active');
      }
    }
  }

  // Apply saved theme on load
  var current = getTheme();
  applyTheme(current);

  // Wire up theme cards when DOM is ready
  function initConfigCards() {
    var cards = document.querySelectorAll('.theme-card');
    for (var i = 0; i < cards.length; i++) {
      (function(card) {
        card.addEventListener('click', function() {
          var theme = card.getAttribute('data-theme');
          setTheme(theme);
        });
      })(cards[i]);
    }
    updateConfigCards(current);
  }

  // If DOM already loaded, init now; otherwise wait
  if (document.readyState === 'loading') {
    document.addEventListener('DOMContentLoaded', initConfigCards);
  } else {
    initConfigCards();
  }

  // Expose global API
  window.setTheme = setTheme;
  window.getTheme = getTheme;
})();
