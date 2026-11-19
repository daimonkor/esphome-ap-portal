// Pulls the page scripts out of landing_page.h for the jsdom tests.
const fs = require('fs'), path = require('path');
const src = fs.readFileSync(path.join(__dirname, '../../components/ap_portal/landing_page.h'), 'utf8');
const raw = (name, tag) => src.match(new RegExp(name + '\\[\\] = R"' + tag + '\\(([\\s\\S]*?)\\)' + tag + '"'))[1];
module.exports = {
  popup: () => raw('POPUP_JS', 'JS'),
  client: () => raw('CLIENT_JS', 'JS'),
  wifiPage: () => raw('BUILTIN_WIFI', 'HTML')
    .replace(/\{\{[#/]if [^}]*\}\}|\{\{\/if\}\}|\{\{else\}\}/g, '')
    .replace(/\{\{[^}]*\}\}/g, 'x')
    .replace('<script src="/ap_portal/client.js"></script>', ''),
};
