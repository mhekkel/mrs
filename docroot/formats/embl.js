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

		var table = $("<table class='list sub_entry' cellspacing='0' cellpadding='0' width='100%'/>");

		var s = '';
		if (ref.ra != null && ref.ra.length > 0) s += ref.ra;
		if (ref.rt != null && ref.rt.length > 0) s += "<strong>" + ref.rt + "</strong>";
		if (ref.rl != null && ref.rl.length > 0) s += ' ' + ref.rl;
		if (s.length > 0) s += "<br/>";

		var a, rx = new Array(), re = /(DOI|PUBMED); (.+)\.$/gm;
		
		while ((a = re.exec(ref.rx)) != null)
		{
			var url;
		
			if (a[1] == 'MEDLINE')
				url = "<a href='http://www.ncbi.nlm.nih.gov/entrez/query.fcgi?cmd=Retrieve&amp;db=PubMed&amp;list_uids=" + a[2] + "&amp;dopt=Abstract'>" + a[2] + "</a>";
			else if (a[1] == 'PUBMED')
				url = "<a href='http://www.ncbi.nlm.nih.gov/entrez/query.fcgi?db=pubmed&amp;cmd=search&amp;term=" + a[2] + "'>" + a[2] + "</a>";
			else if (a[1] == 'DOI')
				url = "<a href='http://dx.doi.org/" + a[2] + "'>" + a[2] + "</a>";
		
			if (url != null)
				$("<tr/>").append(
					$("<td width='40%' class='label'/>").append(a[1]),
					$("<td/>").append(url)
				).appendTo(table);
		}

		if (ref.rp != null && ref.rp.length > 0)
			$("<tr/>").append(
				$("<td class='label'/>").append("Reference Position"),
				$("<td/>").append(ref.rp.toLowerCase())
			).appendTo(table);

		$(ref.rc.split(/;\s*/)).each(function(index, value) {
			if (value.length > 0)
				$("<tr/>").append(
					$("<td class='label'/>").append("Reference Comment"),
					$("<td/>").append(value.toLowerCase())
				).appendTo(table);
		});

		if (ref.rg != null && ref.rg.length > 0)
			$("<tr/>").append(
				$("<td class='label'/>").append("Reference Group"),
				$("<td/>").append(ref.rg)
			).appendTo(table);
		
		s += table.html();
		return "<td>" + ref.nr + "</td><td>" + s + "</td>";
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

		var nl = '\n';
		if ($.browser.msie && $.browser.version < 9)
			nl = '\r';
		
		var result = $("<pre/>"), i = 0;
		for (j in Embl.segments) {
			var s = '';
			for (var k = Embl.segments[j].from; k <= Embl.segments[j].to; ++k) {
				s += seq[i];
				++i;
				if (i % 60 == 0) s += nl;
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
		var ftbl, floc, atbl;
	
		var re = /^(([A-Z]{2})   ).+\n(\2.+\n)*/gm;
		
		var aso = [ 5, 21, 42, 57 ];
		
		var m;
		while ((m = re.exec(text)) != null) {
			if (m[2] == 'ID') {
				info.push(Embl.cell("Entry name",
					m[0].replace(/ID\s+([^;]+)(;.+)/, "<strong>$1</strong>$2")));
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
					return "<a href='link?db=taxonomy&amp;ix=sn&amp;id=" + $("<span/>").text(v).text() + "'>" + v + "</a>";
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
					var attr = value;
					if (attr.indexOf('<') >= 0)
						attr = $("<span/>").text(attr).text();
					return "<a href='search?db=embl&amp;q=kw:\"" + attr + "\"'>" + value + "</a>";
				});
				desc.push(Embl.cell("Keywords", a.join(", ")));
			}
			else if (m[2] == 'RN') {
				var ref = { nr: m[0].replace(/^RN   \[(\d+)\].*/, "$1"),
					rp: "", rx: "", rc: "", rg: "", ra: "", rt: "", rl: "" };
				refs.push(ref);
			}
			else if (m[2] == 'RP') { refs[refs.length - 1].rp += m[0].replace(/^RP   (.+)\n?$/gm, "$1"); }
			else if (m[2] == 'RX') { refs[refs.length - 1].rx += m[0]; }
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
				cmnt.push($("<td colspan='2'/>").append(m[0].replace(/^CC   /gm, '')));
			}
			else if (m[2] == 'AH') {
				aso[0] = m[0].indexOf('LOCAL_SPAN');
				aso[1] = m[0].indexOf('PRIMARY_IDENTIFIER');
				aso[2] = m[0].indexOf('PRIMARY_SPAN');
				aso[3] = m[0].indexOf('COMP');
			}
			else if (m[2] == 'AS') {
				
				atbl = $("<table class='list sub_entry' cellspacing='0' cellpadding='0' width='100%'/>");
				atbl.append(
					$("<tr/>").append(
						$("<th width='10%'/>").append('Primary seq.'),
						$("<th width='10%'/>").append('Span in assembly'),
						$("<th width='80%'/>").append('Span in primary sequence')
					)
				);
			
				var rx = /^.+$/gm;
				var mx;
				while ((mx = rx.exec(m[0])) != null)
				{
					var s = mx[0].replace(/<[^>]+>/g, '');
				
					var localSpan = s.substr(aso[0], aso[1] - aso[0]);
					var primIdent = s.substr(aso[1], aso[2] - aso[1]);
					var primSpan  = s.substr(aso[2], aso[3] - aso[2]);
					var comp      = s.substr(aso[3]) == 'c' ? ' (complement)' : '';
					
					atbl.append(
						$("<tr/>").append(
							$("<td/>").append(localSpan),
							$("<td/>").append(primIdent),
							$("<td/>").append(primSpan + comp)
						)
					);
				}
			}
			else if (m[2] == 'FT') {
				// the feature table...
				
				ftbl = $("<table class='list' cellspacing='0' cellpadding='0' width='100%'/>");
				var featureNr = 1;
				floc = new Array();
				
				$("<tr/>").append(
					$("<th/>").append('Key'),
					$("<th width='8%'/>").append('From'),
					$("<th width='8%'/>").append('To'),
					$("<th width='8%'/>").append('Length'),
					$("<th/>").append('Qualifier'),
					$("<th/>").append('Value')
				).appendTo(ftbl);
				
				var s = m[0].replace(/^FT   /gm, '');
				
				var rx = /^([^ ].{14}) (join\([^)]+?\)|((&lt;)?\d+)(\.\.((&gt;)?\d+))?)((\n {15}.+)*)/gm;
				while ((m = rx.exec(s)) != null) {
					
					var len = 0;
					try { len = m[6] - m[3] + 1; } catch (e) {}
					
					if (m[3] != null && m[6] != null) {
						var loc = { from: m[3].replace("&lt;", ''), to: m[6].replace("&gt;", ''), length: len };
						floc.push(loc);
					}
					else {
						floc.push({});
					}
					Embl.features.push(new Array());
					
					var featureId = "feature-" + featureNr++;
					
					var rxf = /\/([^=]+)=("(([^<"]|<[^>]+>)+)"|\d+)/gm;
					
					var fv = new Array();
					var m2;
					while ((m2 = rxf.exec(m[8])) != null)
					{
						var fvv = { name: m2[1], value: m2[3] ? m2[3] : m2[2] };
						
						if (fvv.name == 'translation')
						{
							fvv.value = $("<div/>").addClass("scrolling-sequence").append(
								fvv.value.replace(/\s+/g, '').replace(/.{40}/g, "$&\n")
							);
						}
						
						fv.push(fvv);
					}
					
					if (fv.length == 0)
						fv.push({name: '', value: ''});
					
					var row = $("<tr/>").append(
						$("<td/>").attr('rowspan', fv.length).append(m[1].toLowerCase()));
						
					if (m[3] != null) {
						row.append(
							$("<td class='right'/>").attr('rowspan', fv.length).append(m[3]),
							$("<td class='right'/>").attr('rowspan', fv.length).append(m[6]),
							$("<td class='right'/>").attr('rowspan', fv.length).append(len ? len : '')
						);
					}
					else {
						row.append(
							$("<td colspan='3'/>").attr('rowspan', fv.length)
								.append(m[2].replace(/,/g, ",\n")));
					}
					
					row.append(
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

		if (xref.length > 0)
		{
			$("<tr/>").append("<th colspan='2'>Cross-references</th>").appendTo(table);
			$(xref).each(function(index, value) {
				$("<tr/>").append(value).appendTo(table);
			});
		}
		
		if (cmnt.length > 0) {
			$("<tr/>").append("<th colspan='2'>Comments</th>").appendTo(table);
			$(cmnt).each(function(index, value) {
				$("<tr/>").append(value).appendTo(table);
			});
		}

		if (atbl != null)
		{
			$("<tr/>").append("<th colspan='2'>Assembly information</th>").appendTo(table);
			$("<tr/>").append(
				$("<td colspan='2' class='sub_entry'/>")
					.append($("<div/>").addClass('feature_table').append(atbl))).appendTo(table);
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
	}
}

Format.toHtml = function(text) {
	return Embl.toHtml(text);
}
