UniProt = {
	ref: null,
	comments: null,
	cross: null,

	cell: function(name, value) {
		return  "<td width='20%'>" + name + "</td>" +
				"<td>" + value + "</td>";
	},

	init: function() {
		var info = new Array();
		var name = new Array();
	
		var entry = $("#entry");
		var text = $("#entrytext").html();
		
		var table = $("<table cellspacing='0' cellpadding='0' width='100%'/>");
		table.append($("<tr/>").append("<th colspan='2'>Entry information</th>"));
		
		var re = /^(([A-Z]{2})   ).+\n(\2.+\n)*/gm;
		
		var m;
		while ((m = re.exec(text)) != null) {
			if (m[2] == 'ID') {
				info.push(UniProt.cell("Entry name",
					m[0].replace(/ID\s+(\S+)\s+(.+)/, "<strong>$1</strong> $2</td>")));
			}
			else if (m[2] == 'AC') {
				var a = m[0].replace(/^AC\s+/gm, '').split(/;\s*/);
				info.push(UniProt.cell("Primary accession", "<strong>" + a.shift() + "</strong>"));
				info.push(UniProt.cell("Secondary accession", a.join(" ")));
			}
			else if (m[2] == 'DT') {
				var dates = m[0].replace(/\.$/gm, '').split("\n");
				for (i in dates) {
					info.push(UniProt.cell(dates[i].substr(18), dates[i].substr(5, 11)));
				}
			}
		}
		
		$(info).each(function(index) {
			table.append($("<tr/>").append(info[index]));
		});
		
		entry.prepend(table);
	}
}

addLoadEvent(UniProt.init);
