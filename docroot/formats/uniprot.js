//	UniProt formatting code in JavaScript.
//
//	The formatter is implemented as a class since it is somewhat more complex than average.
//	To parse DE lines, a sub class is used, this subclass is a recursive descent parser.

UniProt = {

	// DE line Parser
	parser: {
		END: 0, RECNAME: 1, ALTNAME: 2, SUBNAME: 3, FULL: 4, SHORT: 5, EC: 6, ALLERGEN: 7,
		BIOTECH: 8, CD_ANTIGEN: 9, INN: 10, TEXT: 11, INCLUDES: 12, CONTAINS: 13, FLAGS: 14,
		
		parse: function(de) {
			this.text = de;
			this.lookahead = this.getNextToken();
			try {
				if (this.lookahead == this.RECNAME)
				{
					this.matchToken(this.RECNAME);
					this.name = this.parseNextName();
				}
				
				for (;;)
				{
					if (this.lookahead == this.INCLUDES) {
						this.matchToken(this.INCLUDES);
						if (this.includes == null) this.includes = new Array();
						this.includes.push(this.parseNextName());
						continue;
					}
					
					if (this.lookahead == this.CONTAINS) {
						this.matchToken(this.CONTAINS);
						if (this.contains == null) this.contains = new Array();
						this.contains.push(this.parseNextName());
						continue;
					}
					
					if (this.lookahead == this.SUBNAME) {
						this.matchToken(this.SUBNAME);
						if (this.sub == null) this.sub = new Array();
						this.sub.push(this.parseNextName());
						continue;
					}
					
					break;
				}
			
				while (this.lookahead == this.FLAGS)
				{
					this.matchToken(this.FLAGS);
					if (this.flags == null) this.flags = new Array();
					this.flags.push(this.value);
					this.matchToken(this.TEXT);
				}
			}
			catch (e) {
				alert(e);
			}
		},
	
		parseNextName: function() {
			var result = {};
			
			if (this.lookahead == this.RECNAME || this.lookahead == this.SUBNAME)
				this.matchToken(this.lookahead);
		
			result.name = this.parseName();
			
			while (this.lookahead == this.ALTNAME)
			{
				this.matchToken(this.ALTNAME);
				if (result.alt == null) result.alt = new Array;
			
				if (this.lookahead == this.ALLERGEN) {
					this.matchToken(this.ALLERGEN);
					result.alt.push("allergen: " + this.value);
					this.matchToken(this.TEXT);
				}
				else if (this.lookahead == this.BIOTECH) {
					this.matchToken(this.BIOTECH);
					result.alt.push("biotech: " + this.value);
					this.matchToken(this.TEXT);
				}
				else if (this.lookahead == this.CD_ANTIGEN) {
					this.matchToken(this.CD_ANTIGEN);
					result.alt.push("cd antigen: " + this.value);
					this.matchToken(this.TEXT);
				}
				else if (this.lookahead == this.INN) {
					this.matchToken(this.INN);
					result.alt.push("INN: " + this.value);
					this.matchToken(this.TEXT);
				}
				else {
					result.alt.push(this.parseName());
				}
			}
			
			return result;
		},
		
		parseName: function() {
			var name = {};
		
			for (;;)
			{
				if (this.lookahead == this.FULL) {
					this.matchToken(this.FULL);
					name.full = this.value;
					this.matchToken(this.TEXT);
					continue;
				}
				
				if (this.lookahead == this.SHORT) {
					this.matchToken(this.SHORT);
					if (name.short == null)
						name.short = new Array();
					name.short.push(this.value);
					this.matchToken(this.TEXT);
					continue;
				}
				
				if (this.lookahead == this.EC) {
					this.matchToken(this.EC);
					if (name.ec == null)
						name.ec = new Array();
					name.ec.push(this.value);
					this.matchToken(this.TEXT);
					continue;
				}
				
				break;
			}
			
			var result;
			if (name.full != null) {
				result = name.full;
				if (name.short != null)
					result += " (" + name.short.join("; ") + ")";
			}
			else if (name.short != null)
				result = name.short.join("; ");
		
			if (name.ec != null)
				result += ", " + name.ec.join("; ");
		
			return result;
		},
		
		matchToken: function(token) {
			if (token != this.lookahead)
				throw "Parse error in DE record";
			this.lookahead = this.getNextToken();
		},
		
		getNextToken: function() {
			var result = this.END;
			
			if (this.text != null && this.text.length > 0)
			{
				var m = this.text.match(/^\s*([^ =:]+)(:|=)\s*/);
				if (m != null && m.length > 2)
				{
					     if (m[1] == 'RecName')		{ result = this.RECNAME; }
					else if (m[1] == 'AltName')		{ result = this.ALTNAME; }
					else if (m[1] == 'SubName')		{ result = this.SUBNAME; }
					else if (m[1] == 'Full')		{ result = this.FULL; }
					else if (m[1] == 'Short')		{ result = this.SHORT; }
					else if (m[1] == 'EC')			{ result = this.EC; }
					else if (m[1] == 'Allergen')	{ result = this.ALLERGEN; }
					else if (m[1] == 'Biotech')		{ result = this.BIOTECH; }
					else if (m[1] == 'CD_antigen')	{ result = this.CD_ANTIGEN; }
					else if (m[1] == 'INN')			{ result = this.INN; }
					else if (m[1] == 'Includes')	{ result = this.INCLUDES; }
					else if (m[1] == 'Contains')	{ result = this.CONTAINS; }
					else if (m[1] == 'Flags')		{ result = this.FLAGS; }
					
					if (result != this.END)
						this.text = this.text.substr(m[0].length);
				}
			
				if (result == this.END) {	// none of the above, so it must be 'text'
					m = this.text.match(/(.+?);?$/m);
					if (m != null) {
						this.value = m[1];
						this.text = this.text.substr(m[0].length);
						result = this.TEXT;
					}
				}
			}
			
			return result;
		}
	},

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
			arr.push(UniProt.cell(label, name.name, rowspan));
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
		seq = seq.replace(/\s+/gm, '').split('');
		
		var aaf = new Array();
		for (i in seq)
			aaf[i] = "";
		
		for (i in floc) {
			if (floc[i].length == 0) continue;
			for (var j = floc[i].from - 1; j < floc[i].to; ++j)
				if (aaf[j].length == 0) aaf[j] += i; else aaf[j] += ';' + i;
		}
		
		for (var i = 0; i < aaf.length; ++i) {
			var j = i + 1;
			while (j < aaf.length && aaf[j] == aaf[i]) ++j;
			
			var segment = { from: i, to: j - 1, features: aaf[i] };
			UniProt.segments.push(segment);
			i = j - 1;
		}
		
		var nl = '\n';
		if ($.browser.msie && $.browser.version < 9)
			nl = '\r';
		
		var result = $("<pre/>"), i = 0;
		for (j in UniProt.segments) {
			var s = '';
			for (var k = UniProt.segments[j].from; k <= UniProt.segments[j].to; ++k) {
				s += seq[i];
				++i;
				if (i % 60 == 0) s += nl;
				else if (i % 10 == 0) s += ' ';
			}
			result.append($("<span class='segment'/>").attr("id", "segment-" + j).append(s));
			$.each(UniProt.segments[j].features.split(';'), function(index, value) {
				if (value != null && value.length > 0)
					UniProt.features[value].push(j);
			});
		}
	
		return result;
	},

	toHtml: function(text) {
		var info = new Array();
		var name = new Array();
		var refs = new Array();
		var cmnt = new Array();
		var xref = new Array();
		var seqr = new Array();
		var ftbl, floc;
		var copyright;
	
		var re = /^(([A-Z]{2})   ).+\n(\2.+\n)*/gm;
		
		var m;
		while ((m = re.exec(text)) != null) {
			if (m[2] == 'ID') {
				info.push(UniProt.cell("Entry name",
					m[0].replace(/ID\s+((<[^<>]+>|[^ <])+)\s+(.+)/, "<strong>$1</strong> $3")));
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
			else if (m[2] == 'PE') {
				info.push(UniProt.cell("Protein existence", m[0].substr(5)));
			}
			else if (m[2] == "DE") {
				var parser = UniProt.parser;
				parser.parse(m[0].replace(/^DE   /gm, ''));

				if (parser.name != null)
					UniProt.addName("Protein Name", parser.name, name);
				
				for (i in parser.sub)
					UniProt.addName("Submitted Name", parser.sub[i], name);
				
				for (i in parser.contains)
					UniProt.addName("Contains", parser.contains[i], name);

				for (i in parser.includes)
					UniProt.addName("Includes", parser.includes[i], name);

				for (i in parser.flags)
					name.push("<td>Flags</td><td>" + parser.flags[i] + "</td>");
			}
			else if (m[2] == "OS") {
				name.push(UniProt.cell("From", m[0].replace(/^OS   /, '')));
			}
			else if (m[2] == "OX") {
				name.push(UniProt.cell("NCBI Taxonomy ID", m[0].replace(/^OX   NCBI_TaxID=(\d+);/, function(s,v) {
					return "<a href='entry?db=taxonomy&amp;id=" + $("<span/>").text(v).text() + "'>" + v + "</a>";
				})));
			}
			else if (m[2] == "OC") {
				var a = m[0].replace(/^OC   /gm, '').replace(/\.\s*$/mg, '').split(/;\s*/);
				var b = $.map(a, function(v) {
					return "<a href='link?db=taxonomy&amp;ix=sn&amp;id=" + $("<span/>").text(v).text() + "'>" + v + "</a>";
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
					var attr = value.replace(/<[^>]*>/g, '');
					return "<a href='search?db=sprot&amp;q=kw:\"" + attr + "\"'>" + value + "</a>";
				});
				name.push(UniProt.cell("Keywords", a.join(", ")));
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
			else if (m[2] == 'CC') {
				var s = m[0].replace(/^CC   /gm, '');
				
				var rx = /^\-\!\- ([A-Z ]+):(.*\n(    .+\n)*)/gm;
				var mx;
				while ((mx = rx.exec(s)) != null) {
					if (mx[1] == 'ALTERNATIVE PRODUCTS') {
						var event, comment, isoforms = new Array();
					
						var rx2 = /^    (\w+)=([^;]+);\s*(.*\n(      .+\n)*)/gm;
						var mx2;
						while ((mx2 = rx2.exec(mx[2])) != null) {
							if (mx2[1] == 'Event') {
								event = mx2[2];
								if ((mx2 = (/Comment=(.+(\n.+)*)/m).exec(mx2[3])) != null) {
									comment = mx2[1];
								}
							}
							else if (mx2[1] == 'Name') {
								var rx3 = /(\w+)=([^;]+);\s*/gm;
								var mx3;
								var isoform = { name: mx2[2], note: '' };
								
								while ((mx3 = rx3.exec(mx2[3])) != null) {
									     if (mx3[1] == 'Synonyms') { isoform.synonym = mx3[2]; }
									else if (mx3[1] == 'IsoId') { isoform.isoid = mx3[2]; }
									else if (mx3[1] == 'Sequence') { isoform.seq = mx3[2]; }
									else if (mx3[1] == 'Note') { isoform.note += ' ' + mx3[2]; }
								}
								
								isoforms.push(isoform);
							}
						}
						
						if (isoforms.length > 0 && event != null)
						{
							if (comment != null) event += ' ' + comment;
							
							var table = $(document.createElement("table")).attr("cellpadding", 0)
								.attr("cellspacing", 0).attr("width", '100%');
							$("<tr/>").append(
								$("<th/>").append('Name'),
								$("<th/>").append('Synonyms'),
								$("<th/>").append('IsoId'),
								$("<th/>").append('Sequence'),
								$("<th/>").append('Note')
							).appendTo(table);

							$(isoforms).each(function(index,value) {
								$("<tr/>").append(
									$("<td/>").append(value.name),
									$("<td/>").append(value.synonym),
									$("<td/>").append(value.isoid),
									$("<td/>").append(value.seq),
									$("<td/>").append(value.note)
								).appendTo(table);
							});
							event += $("<table/>").append(table).html();
						
							cmnt.push(UniProt.cell2('alternative products', event));
						}
					}
					else {
						cmnt.push("<td>" + mx[1].toLowerCase() + "</td><td>" + mx[2] + "</td>");
					}
				}
				
				if ((mx = (/^(--+\n)(([^-]+.+\n)+)\1/m).exec(s)) != null) {
					copyright = mx[2];
				}
			}
			else if (m[2] == 'DR') {
				var rx = /^DR   ([^;]+);\s*(.+)/gm;
				var mx;
				while ((mx = rx.exec(m[0])) != null) {
					xref.push("<td>" + mx[1] + "</td><td>" + mx[2] + "</td>");
				}
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
					$("<th width='75%'/>").append('Description')
				).appendTo(ftbl);
				
				var s = m[0].replace(/^FT   /gm, '');
				
				var rx = /^([^ ].{7}) (.{6}) (.{6})( (.+(\n {29}.+)*\n?))?/gm;
				while ((m = rx.exec(s)) != null) {
					
					var len = 0;
					try { len = m[3] - m[2] + 1; } catch (e) {}
					var loc = { from: m[2], to: m[3], length: len };

					floc.push(loc);
					UniProt.features.push(new Array());
					
					var featureId = "feature-" + featureNr++;
					
					$("<tr/>").append(
						$("<td/>").append(m[1].toLowerCase()),
						$("<td class='right'/>").append(m[2]),
						$("<td class='right'/>").append(m[3]),
						$("<td class='right'/>").append(len ? len : ''),
						$("<td/>").append(m[4] ? m[4].replace(/\n *(?=\/)/gm, '<br/>') : '')
					).attr('id', featureId).addClass('feature')
					.click(function() {
						$('.feature').removeClass('highlighted');
						$('#' + this.id).addClass('highlighted');
						$('.segment').removeClass('highlighted');
						var nr = this.id.substr("feature-".length) - 1;
						$.each(UniProt.features[nr], function(index, value) {
							$("#segment-" + value).addClass('highlighted');
						});
					})
					.mouseover(function() {
						var nr = this.id.substr("feature-".length) - 1;
						$.each(UniProt.features[nr], function(index, value) {
							$("#segment-" + value).addClass('hover-feature');
						});
					 })
					.mouseleave(function() {
						var nr = this.id.substr("feature-".length) - 1;
						$.each(UniProt.features[nr], function(index, value) {
							$("#segment-" + value).removeClass('hover-feature');
						});
					})
					.appendTo(ftbl);
				}
			}
			else if (m[2] == 'SQ') {
				var aa = 0, mw = 0, crc = 0;
				var rx = /^SQ   SEQUENCE\s*(\d+)\s*AA;\s*(\d+)\s*MW;\s*(.+)\s*CRC64;/;
				
				if ((m = rx.exec(m[0])) != null) {
					aa = m[1];
					mw = m[2];
					crc = m[3];
				}
				
				seqr.push($("<td colspan='2'/>").append(
					"Length: <strong>" + aa + "</strong>, molecular weight <strong>" + mw + "</strong>, " +
					  "CRC64 checksum <strong>" + crc + "</strong>")
				);
				
				rx = /^SQ   .*\n((     .+\n)+)/m;
				if ((m = rx.exec(text)) != null) {
					seqr.push($("<td colspan='2'/>").addClass('sequence')
						.append(UniProt.createSequence(m[1], floc)));
				}
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
		
		$("<tr/>").append("<th colspan='2'>References</th>").appendTo(table);
		$(refs).each(function(index, value) {
			$("<tr/>").append(UniProt.createReference(value)).appendTo(table);
		});

		$("<tr/>").append("<th colspan='2'>Comments</th>").appendTo(table);
		$(cmnt).each(function(index, value) {
			$("<tr/>").append(value).appendTo(table);
		});
		
		if (copyright != null) {
			$("<tr/>").append("<th colspan='2'>Copyright</th>").appendTo(table);
			$("<tr/>").append("<td colspan='2'>" + copyright + "</td>").appendTo(table);
		}
		
		$("<tr/>").append("<th colspan='2'>Cross-references</th>").appendTo(table);
		$(xref).each(function(index, value) {
			$("<tr/>").append(value).appendTo(table);
		});
		
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
	return UniProt.toHtml(text);
}

