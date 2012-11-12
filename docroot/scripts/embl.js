//	Embl formatting code in JavaScript.
//
//	The formatter is implemented as a class since it is somewhat more complex than average.
//	To parse DE lines, a sub class is used, this subclass is a recursive descent parser.

Embl = {
	segments: new Array(),
	features: new Array(),
	
	cell: function(name, value, rowspan) {
		if (rowspan == null)
			rowspan = '';
		else
			rowspan = ' rowspan=\'' + rowspan + '\'';
	
		return  "<td width='20%'" + rowspan + ">" + name + "</td>" +
				"<td>" + value + "</td>";
	},

	cell2: function(name, value) {
		return  "<td width='20%'>" + name + "</td>" +
				"<td class='sub_entry'>" + value + "</td>";
	},

	addName: function(label, name, arr) {
		if (name != null)
		{
			var rowspan;
			if (name.alt != null)
				rowspan = name.alt.length + 1;
			arr.push(Embl.cell(label, name.name, rowspan));
			for (i in name.alt)
				arr.push("<td>" + name.alt[i] + "</td>");
		}
	},
	
	createReference: function(ref) {
		var s = '';
		if (ref.ra != null && ref.ra.length > 0) s += ref.ra;
		if (ref.rt != null && ref.rt.length > 0) s += "<strong>" + ref.rt + "</strong>";
		if (ref.rl != null && ref.rl.length > 0) s += ' ' + ref.rl;
		if (s.length > 0) s += "<br/>";

		// be very careful here, the rx may contain a ';' inside a DOI specifier e.g.
		ref.rx = ref.rx.replace(/;\s+/g, '\n').replace(/;$/, '');

		var rx = $(ref.rx.split(/\n/)).map(function(index,value) {
			var a = value.split(/=/);
			if (a.length == 2) {
				var e = a[1];
				if (a[0] == 'MEDLINE')
					e = "<a href='http://www.ncbi.nlm.nih.gov/entrez/query.fcgi?cmd=Retrieve&amp;db=PubMed&amp;list_uids=" + a[1] + "&amp;dopt=Abstract'>" + a[1] + "</a>";
				else if (a[0] == 'PubMed')
					e = "<a href='http://www.ncbi.nlm.nih.gov/entrez/query.fcgi?db=pubmed&amp;cmd=search&amp;term=" + a[1] + "'>" + a[1] + "</a>";
				else if (a[0] == 'DOI')
					e = "<a href='http://dx.doi.org/" + a[1] + "'>" + a[1] + "</a>";
				return "<td class='label'>" + a[0] + "</td><td>" + e + "</td>";
			}
			if (value.length > 0)
				return "<td colspan='2'>" + a[0] + "</td>";
			return "";
		});

		var rows = new Array();

		if (rx.length > 0) {
			$.each(rx, function(index, value) {
				if (value.length > 0)
					rows.push(value);
			});
		}
		
		if (ref.rp != null && ref.rp.length > 0) rows.push("<td class='label'>Reference Position</td><td>" + ref.rp.toLowerCase() + "</td>");
		$(ref.rc.split(/;\s*/)).each(function(index, value) {
			if (value.length > 0)
				rows.push("<td class='label'>Reference Comment</td><td>" + value.toLowerCase() + "</td>");
		});
		if (ref.rg != null && ref.rg.length > 0) rows.push("<td class='label'>Reference Group</td><td>" + ref.rg + "</td>");
	
		if (rows.length > 0)
		{
			var table = $(document.createElement("table")).attr("cellpadding", 0)
				.attr("cellspacing", 0).attr("width", '100%');
			$(rows).each(function(index,value) { $("<tr/>").append(value).appendTo(table); });
			s += $("<table/>").append(table).html();
		}

		return "<td>" + ref.nr + "</td><td class='sub_entry'>" + s + "</td>";
	},

	createSequence: function(seq, floc) {
		seq = seq.replace(/\s+/gm, '');
		
		var aaf = new Array();
		for (i in seq) aaf[i] = "";
		
		for (i in floc) {
			if (floc[i].length == 0) continue;
			for (var j = floc[i].from - 1; j < floc[i].to; ++j)
				if (aaf[j].length == 0) aaf[j] = i; else aaf[j] += ';' + i;
		}
		
		for (var i = 0; i < aaf.length; ++i) {
			var j = i + 1;
			while (j < aaf.length && aaf[j] == aaf[i]) ++j;
			
			var segment = { from: i, to: j - 1, features: aaf[i] };
			Embl.segments.push(segment);
			i = j - 1;
		}
		
		var result = $("<pre/>"), i = 0;
		for (j in Embl.segments) {
			var s = '';
			for (var k = Embl.segments[j].from; k <= Embl.segments[j].to; ++k) {
				s += seq[i];
				++i;
				if (i % 60 == 0) s += '\n'
				else if (i % 10 == 0) s += ' ';
			}
			result.append($("<span class='segment'/>").attr("id", "segment-" + j).append(s));
			$.each(Embl.segments[j].features.split(';'), function(index, value) {
				if (value != null && value.length > 0)
					Embl.features[value].push(j);
			});
		}
	
		return result;
	},

	toHtml: function(text) {
		var info = new Array();
		var cmnt = new Array();
		var desc = new Array();
		var refs = new Array();
		var xref = new Array();
		var seqr = new Array();
		var ftbl, floc;
	
		var re = /^(([A-Z]{2})   ).+\n(\2.+\n)*/gm;
		
		var m;
		while ((m = re.exec(text)) != null) {
			if (m[2] == 'ID') {
				info.push(Embl.cell("Entry name",
					m[0].replace(/ID\s+(\S+?)(;.+)/, "<strong>$1</strong>$2")));
			}
			else if (m[2] == 'AC') {
				var a = m[0].replace(/;\s*$/, '').replace(/^AC\s+/gm, '').split(/;\s*/);
				info.push(Embl.cell("Primary accession", "<strong>" + a.shift() + "</strong>"));
				if (a.length > 0)
					info.push(Embl.cell("Secondary accession", a.join(" ")));
			}
			else if (m[2] == 'SV') {
				info.push(Embl.cell("Sequence version", m[0].substr(5)));
			}
			else if (m[2] == 'DT') {
				var dates = m[0].replace(/\s+$/, '').replace(/\.$/gm, '').split("\n");
				for (i in dates) {
					var rd = /DT   (\d+)-(\S+)-(\d+) \(Rel\. (\d+), ([^),]+)(.*)\)/;
					if (m = rd.exec(dates[i]))
					{
						var datename = m[5];
						var release = "; Release " + m[4];
						if (m[6])
							release += m[6];
					
						var d = new Date(m[3],
							{"JAN":0, "FEB":1, "MAR":2, "APR":3, "MAY":4, "JUN":5, 
							"JUL":6, "AUG":7, "SEP":8, "OCT":9, "NOV":10, "DEC":11}[m[2]],
							m[1]);
						info.push(Embl.cell(datename, d.toDateString() + release));
					}
				}
			}
			else if (m[2] == 'PE') {
				info.push(Embl.cell("Protein existence", m[0].substr(5)));
			}
			else if (m[2] == "DE") {
				var parser = Embl.parser;
				desc.push(Embl.cell("Description", m[0].replace(/^DE   /gm, '')));
			}
			else if (m[2] == "OS") {
				desc.push(Embl.cell("From", m[0].replace(/^OS   /, '')));
			}
			else if (m[2] == "OC") {
				var a = m[0].replace(/^OC   /gm, '').replace(/\.\s*$/mg, '').split(/;\s*/);
				var b = $.map(a, function(v) {
					return "<a href='link?db=taxonomy&amp;ix=sn&amp;id=" + $(v).text() + "'>" + v + "</a>";
				});
				desc.push(Embl.cell("Taxonomy", b.join(", ")));
			}
			else if (m[2] == "OG") {
				desc.push(Embl.cell("Encoded on", m[0].replace(/^OG   /g, '')));
			}
			else if (m[2] == "GN") {
				var a = m[0].replace(/^GN   /gm, '').replace(/;\s*$/, '').split(/;\s*/);
				
				var subtable = $(document.createElement("table")).attr("cellpadding", 0)
					.attr("cellspacing", 0).attr("width", '100%');
				$.each(a, function(index, value) {
					value = "<td width='20%'><em>" + value.replace(/=/, "</em></td><td>") + "</td>";
					$("<tr/>").append(value).appendTo(subtable);
				});
				
				desc.push(Embl.cell2("Gene names", $("<table/>").append(subtable).html()));
			}
			else if (m[2] == 'KW') {
				var a = m[0].replace(/^KW   /gm, '').replace(/;\s*$/, '').replace(/\.\n/, '').split(/;\s*/);
				a = $.map(a, function(value) {
					return "<a href='search?db=embl&amp;q=kw:\"" + value + "\"'>" + value + "</a>";
				});
				desc.push(Embl.cell("Keywords", a.join(", ")));
			}
			else if (m[2] == 'RN') {
				var ref = { nr: m[0].replace(/^RN   \[(\d+)\].*/, "$1"),
					rp: "", rx: "", rc: "", rg: "", ra: "", rt: "", rl: "" };
				refs.push(ref);
			}
			else if (m[2] == 'RP') { refs[refs.length - 1].rp += m[0].replace(/^RP   (.+)\n?$/gm, "$1"); }
			else if (m[2] == 'RX') { refs[refs.length - 1].rx += m[0].replace(/^RX   (.+)\n?$/gm, "$1"); }
			else if (m[2] == 'RC') { refs[refs.length - 1].rc += m[0].replace(/^RC   (.+)\n?$/gm, "$1"); }
			else if (m[2] == 'RG') { refs[refs.length - 1].rg += m[0].replace(/^RG   (.+)\n?$/gm, "$1"); }
			else if (m[2] == 'RA') { refs[refs.length - 1].ra += m[0].replace(/^RA   (.+)\n?$/gm, "$1"); }
			else if (m[2] == 'RT') { refs[refs.length - 1].rt += m[0].replace(/^RT   (.+)\n?$/gm, "$1"); }
			else if (m[2] == 'RL') { refs[refs.length - 1].rl += m[0].replace(/^RL   (.+)\n?$/gm, "$1"); }
			else if (m[2] == 'DR') {
				var rx = /^DR   ([^;]+);\s*(.+)/gm;
				var mx;
				while ((mx = rx.exec(m[0])) != null) {
					xref.push("<td>" + mx[1] + "</td><td>" + mx[2] + "</td>");
				}
			}
			else if (m[2] == 'CC') {
				cmnt.push(Embl.cell("Comment", m[0].replace(/^CC   /g, '')));
			}
			else if (m[2] == 'FT') {
				// the feature table...
				
				ftbl = $("<table class='list' cellspacing='0' cellpadding='0' width='100%'/>");
				var featureNr = 1;
				floc = new Array();
				
				$("<tr/>").append(
					$("<th width='10%'/>").append('Key'),
					$("<th width='5%'/>").append('From'),
					$("<th width='5%'/>").append('To'),
					$("<th width='5%'/>").append('Length'),
					$("<th width='10%'/>").append('Qualifier'),
					$("<th width='65%'/>").append('Value')
				).appendTo(ftbl);
				
				var s = m[0].replace(/^FT   /gm, '');
				
				var rx = /^([^ ].{14}) (\d+)\.\.(\d+)\n(( {15}.+\n)+)/gm;
				while ((m = rx.exec(s)) != null) {
					
					var len = 0;
					try { len = m[3] - m[2] + 1; } catch (e) {}
					var loc = { from: m[2], to: m[3], length: len };

					floc.push(loc);
					Embl.features.push(new Array());
					
					var featureId = "feature-" + featureNr++;
					
					var rxf = /\/([^=]+)=(("[^"]+")|\d+)/gm;
					
					var fv = new Array();
					var m2;
					while ((m2 = rxf.exec(m[4])) != null)
					{
						var fvv = { name: m2[1], value: m2[2].replace(/^"(.+)"$/, "$1") };
						
						if (fvv.name == 'translation')
						{
							fvv.value = $("<div/").addClass("scrolling-sequence").append(
								fvv.value.replace(/\s+/g, '').replace(/.{30}/g, "$&\n")
							);
						}
						
						fv.push(fvv);
					}
					
					if (fv.length == 0)
						fv.push({name: '', value: ''});
					
					$("<tr/>").append(
						$("<td/>").attr('rowspan', fv.length).append(m[1].toLowerCase()),
						$("<td class='right'/>").attr('rowspan', fv.length).append(m[2]),
						$("<td class='right'/>").attr('rowspan', fv.length).append(m[3]),
						$("<td class='right'/>").attr('rowspan', fv.length).append(len ? len : ''),
						$("<td/>").append(fv[0].name),
						$("<td/>").append(fv[0].value)
					).attr('id', featureId).addClass('feature')
					.click(function() {
						$('.feature').removeClass('highlighted');
						$('#' + this.id).addClass('highlighted');
						$('.segment').removeClass('highlighted');
						var nr = this.id.substr("feature-".length) - 1;
						$.each(Embl.features[nr], function(index, value) {
							$("#segment-" + value).addClass('highlighted');
						});
					})
					.mouseover(function() {
						var nr = this.id.substr("feature-".length) - 1;
						$.each(Embl.features[nr], function(index, value) {
							$("#segment-" + value).addClass('hover-feature');
						});
					 })
					.mouseleave(function() {
						var nr = this.id.substr("feature-".length) - 1;
						$.each(Embl.features[nr], function(index, value) {
							$("#segment-" + value).removeClass('hover-feature');
						});
					})
					.appendTo(ftbl);
					
					if (fv.length > 1)
					{
						$(fv).each(function(index, value) {
							if (index >= 1)
								$("<tr/>").append(
									$("<td/>").append(value.name),
									$("<td/>").append(value.value)
								).appendTo(ftbl);
						});
					}
					
				}
			}
			else if (m[2] == 'SQ') {
				var aa = 0, bcount;
				var rx = /^SQ   Sequence\s*(\d+)\s*BP;\s*(.+)/;
				
				if ((m = rx.exec(m[0])) != null) {
					bp = m[1];
					bcount = m[2];
				}
				
				seqr.push($("<td colspan='2'/>").append(
					"Length: <strong>" + bp + "</strong> BP, " +
					"A count: <strong>" + /(\d+)\s*A;/.exec(bcount)[1] + "</strong>, " +
					"C count: <strong>" + /(\d+)\s*C;/.exec(bcount)[1] + "</strong>, " +
					"G count: <strong>" + /(\d+)\s*G;/.exec(bcount)[1] + "</strong>, " +
					"T count: <strong>" + /(\d+)\s*T;/.exec(bcount)[1] + "</strong>, " +
					"Other count: <strong>" + /(\d+)\s*other;/.exec(bcount)[1] + "</strong>")
				);
				
				rx = /^SQ   .*\n((     .+\n)+)/m;
				if ((m = rx.exec(text)) != null) {
					seqr.push($("<td colspan='2'/>").addClass('sequence')
						.append(Embl.createSequence(m[1].replace(/[ \n0-9]+/g, ''), floc)));
				}
			}
		}
		
		var table = $("<table cellspacing='0' cellpadding='0' width='100%'/>");

		$("<tr/>").append("<th colspan='2'>Entry information</th>").appendTo(table);
		$(info).each(function(index, value) {
			$("<tr/>").append(value).appendTo(table);
		});

		$("<tr/>").append("<th colspan='2'>Description</th>").appendTo(table);
		$(desc).each(function(index, value) {
			$("<tr/>").append(value).appendTo(table);
		});
		
		if (refs.length > 0)
		{
			$("<tr/>").append("<th colspan='2'>References</th>").appendTo(table);
			$(refs).each(function(index, value) {
				$("<tr/>").append(Embl.createReference(value)).appendTo(table);
			});
		}

		if (cmnt.length > 0) {
			$("<tr/>").append("<th colspan='2'>Comments</th>").appendTo(table);
			$(cmnt).each(function(index, value) {
				$("<tr/>").append(value).appendTo(table);
			});
		}
		
		if (xref.length > 0)
		{
			$("<tr/>").append("<th colspan='2'>Cross-references</th>").appendTo(table);
			$(xref).each(function(index, value) {
				$("<tr/>").append(value).appendTo(table);
			});
		}
		
		if (ftbl != null) {
			$("<tr/>").append($("<th colspan='2'/>").append("Features")).appendTo(table);
			$("<tr/>").append(
				$("<td colspan='2' class='sub_entry'/>")
					.append($("<div/>").addClass('feature_table').append(ftbl))).appendTo(table);
		}

		$("<tr/>").append("<th colspan='2'>Sequence information</th>").appendTo(table);
		$(seqr).each(function(index, value) {
			$("<tr/>").append(value).appendTo(table);
		});
		
		return table;
	},
	
	toFastA: function(text) {
		var id, de, seq;
	
		var re = /^(([A-Z]{2})   ).+\n(\2.+\n)*/gm;
		
		var m;
		while ((m = re.exec(text)) != null) {
			if (m[2] == 'ID') {
				id = m[0].replace(/ID\s+((<[^<>]+>|[^ <])+)\s+(.+)/, "$1").replace(/\s+$/, "");
			}
			else if (m[2] == "DE" && de == null) {
/*
				var parser = Embl.parser;
				parser.parse(m[0].replace(/^DE   /gm, ''));

				if (parser.name != null && parser.name.name != null)
					de = $(parser.name.name).text();  // strip out tags
*/
			}
			else if (m[2] == 'SQ') {
				var rx = /^SQ   .*\n((     .+\n)+)/m;
				if ((m = rx.exec(text)) != null) {
					seq = m[1].replace(/\s+/g, "").replace(/.{72}/g, "$&\n");
				}
			}
		}
		
		return { id: id, de: de, seq: seq };
	}
}

Format.toHtml = function(text) {
	return Embl.toHtml(text);
}

Format.toFastA = function(text) {
	return Embl.toFastA(text);
}
