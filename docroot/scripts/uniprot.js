function DEParser() {
	this.END = 0;
	this.RECNAME = 1;
	this.ALTNAME = 2;
	this.SUBNAME = 3;
	this.FULL = 4;
	this.SHORT = 5;
	this.EC = 6;
	this.ALLERGEN = 7;
	this.BIOTECH = 8;
	this.CD_ANTIGEN = 9;
	this.INN = 10;
	this.TEXT = 11;
	this.INCLUDES = 12;
	this.CONTAINS = 13;
	this.FLAGS = 14;
}
	
DEParser.prototype.parse = function(de) {
	this.text = de;
	this.lookahead = this.getNextToken();
	try {
		if (this.lookahead == this.RECNAME)
			this.name = this.parseNextName();
		
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
}

DEParser.prototype.parseNextName = function() {
	var result = {};

	if (this.lookahead == this.RECNAME)
	{
		this.matchToken(this.RECNAME);
		result.name = this.parseName();
	}
	
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
}

DEParser.prototype.parseName = function() {
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
}

DEParser.prototype.matchToken = function(token) {
	if (token != this.lookahead)
		throw "Parse error in DE record";
	this.lookahead = this.getNextToken();
}

DEParser.prototype.getNextToken = function() {
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

UniProt = {
	ref: null,
	comments: null,
	cross: null,
	
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
					m[0].replace(/ID\s+(\S+)\s+(.+)/, "<strong>$1</strong> $2")));
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
				var parser = new DEParser();
				parser.parse(m[0].replace(/^DE   /gm, ''));

				if (parser.name != null)
					UniProt.addName("Protein Name", parser.name, name);
				
				for (i in parser.sub)
					name.push(info.cell("Submitted Name", parser.sub[i]));
				
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
					return "<a href='entry?db=taxonomy&amp;id=" + v + "'>" + v + "</a>";
				})));
			}
			else if (m[2] == "OC") {
				var a = m[0].replace(/^OC   /gm, '').replace(/\.\s*$/mg, '').split(/;\s*/);
				var b = $.map(a, function(v) {
					return "<a href='link?db=taxonomy&amp;ix=sn&amp;id=" + v + "'>" + v + "</a>";
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
					var v2 = value.replace(/<[^<>]+>/g, '');
					return "<a href='search?db=sprot&amp;q=kw:\"" + v2 + "\"'>" + value + "</a>";
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
