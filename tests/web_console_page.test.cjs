const assert = require('assert');
const fs = require('fs');

const page = fs.readFileSync('apps/orcsdr-tab5/ui/web_console.html', 'utf8');
assert.match(page, /x\.timeout=2000/);
assert.match(page, /x\.onloadend=function\(\)\{ specBusy=false; \};/);
console.log('WEB_CONSOLE_PAGE_OK');
