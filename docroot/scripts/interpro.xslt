<xsl:stylesheet xmlns:xsl="http://www.w3.org/1999/XSL/Transform" xmlns:fn="http://www.w3.org/2005/xpath-functions" version="1.0">
	<xsl:template match="dbinfo">
		<tr>
			<td><span style="text-transform:lowercase;"><xsl:value-of select="@dbname"/></span></td>
			<td><xsl:value-of select="@version"/></td>
			<td style="text-align:right;"><xsl:value-of select="@entry_count"/></td>
			<td><span style="text-transform:lowercase;"><xsl:value-of select="@file_date"/></span></td>
		</tr>
	</xsl:template>

	<xsl:template match="release">
		<table class="list" cellspacing="0" cellpadding="0">
			<tr><th>Databank</th><th>Version</th><th>Entry Count</th><th>File Date</th></tr>
			<xsl:apply-templates/>
		</table>
	</xsl:template>
	
	<xsl:template match="class_list">
		<table cellspacing="0" cellpadding="0">
			<xsl:for-each select="classification">
				<tr>
					<td class="label">
						<a><xsl:attribute name="href">link?db=go&amp;ix=id&amp;id=<xsl:value-of select="substring(@id, 4)"/></xsl:attribute>
							<xsl:value-of select="@id"/>
						</a>
					</td>
					<td><xsl:value-of select="category"/></td>
					<td><xsl:value-of select="description"/></td>
				</tr>
			</xsl:for-each>
		</table>
	</xsl:template>
	
	<xsl:template match="example_list">
		<table cellspacing="0" cellpadding="0">
			<xsl:for-each select="example">
				<tr>
					<td class="label"><xsl:value-of select="@id"/></td>
					<td><xsl:value-of select="category"/></td>
					<td><xsl:value-of select="description"/></td>
				</tr>
			</xsl:for-each>
		</table>
	</xsl:template>
	
	<xsl:template match="pub_list">
		<xsl:for-each select="publication">
			<span>
				<xsl:value-of select="author_list"/>;
				<strong><xsl:value-of select="title"/></strong>;
				<xsl:value-of select="journal"/>
				<xsl:value-of select="location/@volume"/>:<xsl:value-of select="location/@firstpage"/>-<xsl:value-of select="location/@lastpage"/>
			</span>
			<table cellspacing="0" cellpadding="0" width="100%">
				<tr>
					<td class="label" width="20%"><xsl:value-of select="db_xref/@db"/></td>
					<td>
						<a>
							<xsl:attribute name="href">http://www.ncbi.nlm.nih.gov/entrez/query.fcgi?db=pubmed;cmd=search;term=<xsl:value-of select="db_xref/@dbkey"/></xsl:attribute>
							<xsl:value-of select="db_xref/@dbkey"/>
						</a>
					</td>
				</tr>
			</table>
		</xsl:for-each>
	</xsl:template>

	<xsl:template match="found_in">
		<table cellspacing="0" cellpadding="0">
			<xsl:for-each select="rel_ref">
				<tr>
					<td>
						<a>
							<xsl:attribute name="href">link?db=interpro&amp;ix=id&amp;id=<xsl:value-of select="@ipr_ref"/></xsl:attribute>
							Interpro <xsl:value-of select="@ipr_ref"/>
						</a>
					</td>
				</tr>
			</xsl:for-each>
		</table>
	</xsl:template>
	
	<xsl:template match="member_list">
		<table cellspacing="0" cellpadding="0">
			<xsl:for-each select="db_xref">
				<tr>
					<td><xsl:value-of select="@name"/></td>
					<td><xsl:value-of select="@db"/></td>
					<td><xsl:value-of select="@dbkey"/></td>
					<td><xsl:value-of select="@protein_count"/></td>
				</tr>
			</xsl:for-each>
		</table>
	</xsl:template>
	
	<xsl:template match="external_doc_list">
		<table cellspacing="0" cellpadding="0">
			<xsl:for-each select="db_xref">
				<tr>
					<td><xsl:value-of select="@db"/></td>
					<td>
						<a>
							<xsl:attribute name="href">link?db=<xsl:value-of select="@db"/>;id=<xsl:value-of select="@dbkey"/></xsl:attribute>
							<xsl:value-of select="@dbkey"/>
						</a>
					</td>
				</tr>
			</xsl:for-each>
		</table>
	</xsl:template>
	
	<xsl:template match="structure_db_links">
		<table cellspacing="0" cellpadding="0">
			<xsl:for-each select="db_xref">
				<tr>
					<td><xsl:value-of select="@db"/></td>
					<td>
						<a>
							<xsl:attribute name="href">link?db=<xsl:value-of select="@db"/>&amp;ix=id&amp;id=<xsl:value-of select="@dbkey"/></xsl:attribute>
							<span style="text-transform:uppercase"><xsl:value-of select="@dbkey"/></span>
						</a>
					</td>
				</tr>
			</xsl:for-each>
		</table>
	</xsl:template>
	
	<xsl:template match="taxonomy_distribution">
		<table cellspacing="0" cellpadding="0">
			<xsl:for-each select="taxon_data">
				<tr>
					<td><xsl:value-of select="@name"/></td>
					<td style="text-align:right;"><xsl:value-of select="@proteins_count"/></td>
				</tr>
			</xsl:for-each>
		</table>
	</xsl:template>
	
	<xsl:template match="db_xref">
		<a>
			<xsl:attribute name="href">link?db=<xsl:value-of select="@db"/>;id=<xsl:value-of select="@dbkey"/></xsl:attribute>
			<xsl:value-of select="@db"/>:<xsl:value-of select="@dbkey"/>
		</a>
	</xsl:template>
	
	<xsl:template match="cite">
		<a>
			<xsl:attribute name="href">http://www.ncbi.nlm.nih.gov/entrez/query.fcgi?db=pubmed&amp;cmd=search&amp;term=<xsl:value-of select="substring(@idref, 4)"/></xsl:attribute>
			<xsl:value-of select="@idref"/>
		</a>
	</xsl:template>
	
	<xsl:template match="interpro">
		<table class="list" cellspacing="0" cellpadding="0" width="100%">
			<tr>
				<th colspan="2">Entry information</th>
			</tr>
			<tr>
				<td width="20%">ID</td>
				<td><strong><xsl:value-of select="@id"/></strong></td>
			</tr>
			<tr>
				<td>Name</td>
				<td><strong><xsl:value-of select="name"/></strong></td>
			</tr>
			<tr>
				<td>Binding site</td>
				<td><xsl:value-of select="@Binding_site"/></td>
			</tr>
			<tr>
				<td>Short name</td>
				<td><xsl:value-of select="@short_name"/></td>
			</tr>
			<tr>
				<td>Protein Count</td>
				<td><xsl:value-of select="@protein_count"/></td>
			</tr>

			<tr>
				<td>Abstract</td>
				<td><xsl:apply-templates select="abstract"/></td>
			</tr>
			
			<tr>
				<td>Classification</td>
				<td><xsl:apply-templates select="class_list"/></td>
			</tr>

			<!--tr>
				<td>Examples</td>
				<td><xsl:apply-templates select="example_list"/></td>
			</tr-->

			<tr>
				<td>Publications</td>
				<td><xsl:apply-templates select="pub_list"/></td>
			</tr>

			<tr>
				<td>Found in</td>
				<td><xsl:apply-templates select="found_in"/></td>
			</tr>

			<tr>
				<td>Members</td>
				<td><xsl:apply-templates select="member_list"/></td>
			</tr>

			<tr>
				<td>External docs</td>
				<td><xsl:apply-templates select="external_doc_list"/></td>
			</tr>

			<tr>
				<td>Structures</td>
				<td><xsl:apply-templates select="structure_db_links"/></td>
			</tr>

			<tr>
				<td>Taxonomy distribution</td>
				<td><xsl:apply-templates select="taxonomy_distribution"/></td>
			</tr>

		</table>
	</xsl:template>
	
	<xsl:template match="/">
		<div>
			<xsl:apply-templates select="interpro"/>
		</div>
	</xsl:template>

</xsl:stylesheet>
