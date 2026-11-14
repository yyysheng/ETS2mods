# Public licensing and build scope

LICENSE is the effective scope index. The original historical MIT text is retained in licenses/LICENSE.historical-MIT.txt; LICENSE_HISTORY.md maps it to original tags/commits. Historical grants cover original project code within their then-effective scope and are not revoked. Historical asset coverage remains unresolved.

See FUTURE_LICENSING_POLICY.md for prospective relicensing of fully owned original code, including previously MIT-published original code. No prospective proprietary terms are active. Third-party components remain separately licensed.

The public build requires LICENSE_PACKAGE.json, source manifest.sii, public license texts/notices and packaging scripts only. Ownership inventories, personal evidence, local dependency audit paths, activation records and inactive detailed terms are stored outside the repositories; they are not required for a clean checkout.

Historical integrity rule: decode strict UTF-8; replace CRLF pairs with LF; encode UTF-8 and compute SHA-256. Do not trim, remove BOM, change lone CR, normalize spaces or remove/add a final newline. The configuration field historical_mit_sha256_lf uses this rule. LICENSE_HISTORY.md hashes describe original Git blob bytes (LF in the mapped commits); these equal the canonical digest for this history. Physical LF and CRLF checkout digests may differ, without changing text. Archive/directory copy integrity still uses raw byte hashes against that checkout. No historical tag or license text is rewritten by this rule.

Only effective licenses and copyright notices enter end-user packages. Public source/build policy documents and configuration are not end-user package payloads. Future version or mode changes fail packaging until a new profile is reviewed.
