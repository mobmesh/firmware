export function sensitive(raw,catalog){const text=raw.startsWith(':raw ')?raw.slice(5):raw;const entry=catalog.resolve(text);return !entry||entry.sensitive||/^(?:password|set (?:guest\.password|password|prv\.key|bridge\.secret|wifi)|get (?:guest\.password|password|prv\.key|bridge\.secret|wifi))\b/i.test(text);}
export function safeDraft(text,cursor,catalog){return sensitive(text,catalog)?null:{text,cursor};}
export function safeRecord(text,secret=false){return secret?'[Sensitive command/output omitted]':text.replace(/((?:password|secret|private.key)\s*[:=])[^\r\n]*/gi,'$1 [redacted]');}
