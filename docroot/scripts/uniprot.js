UniProt = {
	ref: null,
	comments: null,
	cross: null,

	cell: function(name, value, rowspan) {
		if (rowspan == null)
			rowspan = '';
		else
			rowspan = ' rowspan=' + rowspan;
	
		return  "<td width='20%'" + rowspan + ">" + name + "</td>" +
				"<td>" + value + "</td>";
	},

	cell2: function(name, value) {
		return  "<td width='20%'>" + name + "</td>" +
				"<td class='sub_entry'>" + value + "</td>";
	},

	init: function() {
		var info = new Array();
		var name = new Array();
	
		var entry = $("#entry");
		var text = $("#entrytext").html();
		
		var re = /^(([A-Z]{2})   ).+\n(\2.+\n)*/gm;
		
		var m;
		while ((m = re.exec(text)) != null) {
			if (m[2] == 'ID') {
				info.push(UniProt.cell("Entry name",
					m[0].replace(/ID\s+(\S+)\s+(.+)/, "<strong>$1</strong> $2</td>")));
			}
			else if (m[2] == 'AC') {
				var a = m[0].replace(/;\s*$/, '').replace(/^AC\s+/gm, '').split(/;\s*/);
				info.push(UniProt.cell("Primary accession", "<strong>" + a.shift() + "</strong>"));
				if (a.length > 0)
					info.push(UniProt.cell("Secondary accession", a.join(" ")));
			}
			else if (m[2] == 'DT') {
				var dates = m[0].replace(/\s+$/, '').replace(/\.$/gm, '').split("\n");
				for (i in dates) {
					var d = new Date(dates[i].substr(12, 4),
						{"JAN":0, "FEB":1, "MAR":2, "APR":3, "MAY":4, "JUN":5, 
						"JUL":6, "AUG":7, "SEP":8, "OCT":9, "NOV":10, "DEC":11}[dates[i].substr(8, 3)],
						dates[i].substr(5, 2));
					info.push(UniProt.cell(dates[i].substr(18), d.toDateString()));
				}
			}
			else if (m[2] == "DE") {
				var a = m[0].replace(/^DE   /gm, '').replace(/;\s*$/, '')
					.replace(/(RecName|AltName): /gm, '')
					.replace(/(Full|Short)=/gm, '')
					.split(/;\s*/);
				name.push(UniProt.cell("Protein names", a.shift(), a.length + 1));
				$(a).each(function(index,value) {
					name.push("<td>" + value + "</td>");
				});
			}
			else if (m[2] == "OS") {
				name.push(UniProt.cell("From", m[0].replace(/^OS   /, '')));
			}
			else if (m[2] == "OX") {
				name.push(UniProt.cell("NCBI Taxonomy ID", m[0].replace(/^OX   NCBI_TaxID=(\d+);/, function(s,v) {
					return "<a href='entry?db=taxonomy&amp;id=" + v + "'>" + v + "</a>";
				})));
			}
			else if (m[2] == "OC") {
				var a = m[0].replace(/^OC   /gm, '').replace(/;\s*$/, '').split(/;\s*/);
				var b = $.map(a, function(v) {
					return "<a href='search?q=kw:\"" + v + "\"'>" + v + "</a>";
				});
				name.push(UniProt.cell("Lineage", b.join(", ")));
			}
			else if (m[2] == "GN") {
				var a = m[0].replace(/^GN   /gm, '').replace(/;\s*$/, '').split(/;\s*/);
				
				var subtable = $(document.createElement("table")).attr("cellpadding", 0)
					.attr("cellspacing", 0).attr("width", '100%');
				$.each(a, function(index, value) {
					value = "<td width='20%'><em>" + value.replace(/=/, "</em></td><td>") + "</td>";
					$("<tr/>").append(value).appendTo(subtable);
				});
				
				name.push(UniProt.cell2("Gene names", $("<table/>").append(subtable).html()));
			}
			else if (m[2] == 'KW') {
				var a = m[0].replace(/^KW   /gm, '').replace(/;\s*$/, '').replace(/\.\n/, '').split(/;\s*/);
				a = $.map(a, function(value) {
					return "<a href='search?db=sprot&amp;q=kw:\"" + value + "\"'>" + value + "</a>";
				});
				name.push(UniProt.cell("Keywords", a.join(", ")));
			}
		}
		
		var table = $("<table cellspacing='0' cellpadding='0' width='100%'/>");

		$("<tr/>").append("<th colspan='2'>Entry information</th>").appendTo(table);
		$(info).each(function(index, value) {
			$("<tr/>").append(value).appendTo(table);
		});

		$("<tr/>").append("<th colspan='2'>Name and origin of the protein</th>").appendTo(table);
		$(name).each(function(index, value) {
			$("<tr/>").append(value).appendTo(table);
		});
		
		entry.prepend(table);
	}
}

addLoadEvent(UniProt.init);
